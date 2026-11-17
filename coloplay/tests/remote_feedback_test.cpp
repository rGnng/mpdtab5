#include "remote_feedback.h"
#include "coloplay_usb_frame.h"
#include <cassert>
#include <iostream>
int main(){
  RemoteFeedback peer;peer.state.connected=peer.state.wired=true;
  uint8_t data[CP_BLE_BYTES];cp_header(data,CP_HELLO,0,7);data[8]=64;data[16]=CP_CAP_VOLUME_FLOAT|CP_CAP_VOLUME_PREDICT|CP_CAP_VOLUME_SCALE|CP_CAP_NAVIGATION;
  data[17]=CP_FEATURE_STAR;
  assert(!peer.accept(data,20,8,10));assert(peer.accept(data,20,7,10));
  cp_header(data,CP_STATE,0,7);data[9]=1;data[10]=cp_state_flags(true,123,true,0,true);cp_put32(data+12,1000);cp_put32(data+16,10000);
  assert(peer.accept(data,20,7,100));
  assert(peer.snapshot(100).starSupported&&!peer.snapshot(100).starKnown); // No track metadata yet.
  cp_header(data,CP_VOLUME_STATE,0,7);cp_put_float(data+8,.25f);data[13]=64;cp_put_float(data+16,.25f);
  assert(peer.accept(data,20,7,100));assert(peer.snapshot(200).fresh&&peer.snapshot(200).positionMs==1100);
  assert(peer.snapshot(200).predictionReady&&peer.snapshot(200).displayVolume==.5f);
  assert(peer.prediction.queued(1,1,200));assert(peer.snapshot(210).displayVolume==33.0f/64);
  cp_put16(data+2,1);data[12]=CP_APPLIED;cp_put16(data+14,1);cp_put_float(data+8,(33.0f/64)*(33.0f/64));cp_put_float(data+16,(33.0f/64)*(33.0f/64));
  assert(peer.accept(data,20,7,220));assert(!peer.snapshot(220).volumePending);
  cp_header(data,CP_TRACK,0,7);cp_put32(data+8,123);assert(peer.accept(data,20,7,230));
  assert(peer.snapshot(230).starKnown&&peer.snapshot(230).starred);
  cp_header(data,CP_STATE,0,8);data[9]=1;data[10]=cp_state_flags(true,123,false,0,true);
  assert(!peer.accept(data,20,7,235)&&peer.snapshot(235).starred); // Stale session cannot unstar.
  cp_put32(data+4,7);assert(peer.accept(data,20,7,236));
  assert(peer.snapshot(236).starKnown&&!peer.snapshot(236).starred);
  cp_header(data,CP_TEXT,0,7);data[2]=0x81;data[8]=5;memcpy(data+9,"Title",5);
  assert(peer.accept(data,20,7,240));assert(!strcmp(peer.snapshot(240).title,"Title"));
  data[3]=62;data[8]=2;assert(!peer.accept(data,20,7,240));
  // A missing first/middle fragment cannot publish an empty or partial artist.
  const char *artist="歌手の名前";const unsigned length=strlen(artist);
  cp_header(data,CP_TEXT,0,7);data[2]=0x82;data[3]=11;data[8]=length-11;memcpy(data+9,artist+11,length-11);
  assert(!peer.accept(data,20,7,241)&&!peer.snapshot(241).artist[0]);
  cp_header(data,CP_TEXT,0,7);data[2]=2;data[8]=11;memcpy(data+9,artist,11);assert(peer.accept(data,20,7,242));
  data[2]=0x82;data[3]=12;data[8]=length-12;memcpy(data+9,artist+12,length-12);assert(!peer.accept(data,20,7,243));
  assert(!peer.snapshot(243).artist[0]);
  // Full periodic retransmission repairs the field, including split UTF-8.
  cp_header(data,CP_TEXT,0,7);data[2]=2;data[8]=11;memcpy(data+9,artist,11);assert(peer.accept(data,20,7,244));
  data[2]=0x82;data[3]=11;data[8]=length-11;memcpy(data+9,artist+11,length-11);assert(peer.accept(data,20,7,245));
  assert(!strcmp(peer.snapshot(245).artist,artist)&&!strcmp(peer.snapshot(245).title,"Title"));
  cp_header(data,CP_TRACK,0,7);cp_put32(data+8,123);assert(peer.accept(data,20,7,246));
  assert(!strcmp(peer.snapshot(246).artist,artist)&&!strcmp(peer.snapshot(246).title,"Title"));

  cp_header(data,CP_NAVIGATION,1,7);data[8]=1;data[9]=CP_NAV_FOCUS;cp_put16(data+18,5);
  assert(peer.accept(data,20,7,250));cp_header(data,CP_NAV_TEXT,1,7);data[9]=5;memcpy(data+10,"Focus",5);
  assert(peer.accept(data,20,7,250));assert(!strcmp(peer.snapshot(260).navigation.ui.focus,"Focus"));
  cp_navigation_t detail={};detail.view=1;detail.flags=CP_NAV_FOCUS;strcpy(detail.focus,"Play");strcpy(detail.heading,"Album");
  cp_navigation_header(data,2,7,&detail,true);assert(!peer.accept(data,20,7,270)); // Not negotiated.
  cp_header(data,CP_HELLO,0,7);data[16]=CP_CAP_NAVIGATION;data[17]=CP_FEATURE_STAR|CP_FEATURE_NAV_HEADING;
  assert(peer.accept(data,20,7,271)&&peer.state.navigationHeadingSupported);
  cp_navigation_header(data,2,7,&detail,true);assert(peer.accept(data,20,7,272));
  cp_header(data,CP_NAV_TEXT,2,7);data[9]=4;memcpy(data+10,"Play",4);assert(peer.accept(data,20,7,273));
  assert(!strcmp(peer.snapshot(273).navigation.ui.focus,"Focus"));
  cp_header(data,CP_NAV_HEADING_TEXT,2,7);data[9]=5;memcpy(data+10,"Album",5);assert(peer.accept(data,20,7,274));
  assert(!strcmp(peer.snapshot(274).navigation.ui.focus,"Play")&&!strcmp(peer.snapshot(274).navigation.ui.heading,"Album"));
  assert(!peer.snapshot(6000).fresh&&!peer.snapshot(6000).navigation.fresh);
  assert(!peer.snapshot(6000).starKnown&&!peer.snapshot(6000).starred);
  peer.clear();assert(!peer.snapshot(6000).connected&&!peer.snapshot(6000).predictionReady);
  assert(!peer.state.starSupported&&!peer.state.starKnown&&!peer.state.starred);
  // Legacy hosts have no star feedback. Unknown is different from unstarred.
  peer.state.connected=peer.state.secured=true;cp_header(data,CP_HELLO,0,7);
  assert(peer.accept(data,20,7,6100)&&!peer.state.starSupported);
  cp_header(data,CP_TRACK,0,7);cp_put32(data+8,123);assert(peer.accept(data,20,7,6100));
  cp_header(data,CP_STATE,0,7);data[9]=1;data[10]=cp_state_flags(true,123,true,0,true);
  assert(peer.accept(data,20,7,6101)&&!peer.snapshot(6101).starKnown&&!peer.snapshot(6101).starred);

  // Sleep is independent of MPD availability, and stale sessions cannot blank the device.
  cp_sleep_t sleep={CP_SLEEP_ON|CP_SLEEP_COUNTDOWN,30,10000};
  cp_sleep_packet(data,7,&sleep);assert(!peer.accept(data,20,7,6200));
  cp_header(data,CP_HELLO,0,7);data[17]=CP_FEATURE_SLEEP;assert(peer.accept(data,20,7,6201));
  cp_sleep_packet(data,7,&sleep);assert(peer.accept(data,20,7,6202));
  assert(peer.snapshot(6302).sleep.fresh&&peer.snapshot(6302).sleep.remainingMs==9900);
  cp_put32(data+4,8);assert(!peer.accept(data,20,7,6400));assert(peer.snapshot(6400).sleep.awake);
  assert(!peer.snapshot(8202).sleep.fresh);peer.clear();assert(!peer.snapshot(8203).sleep.supported);

  uint8_t frame[CP_USB_FRAME_BYTES],kind,out[CP_BLE_BYTES];cp_usb_encode(frame,CP_USB_DATA,data);
  for(unsigned chunk=1;chunk<=32;++chunk){
    cp_usb_parser_t parser={};unsigned found=0;
    const char *log="ColoPlay console status\r\n";
    for(const char *p=log;*p;++p)assert(!cp_usb_feed(&parser,*p,&kind,out));
    for(unsigned i=0;i<sizeof(frame);i+=chunk)for(unsigned n=i;n<sizeof(frame)&&n<i+chunk;++n)found+=cp_usb_feed(&parser,frame[n],&kind,out);
    assert(found==1&&kind==CP_USB_DATA&&!memcmp(out,data,20));
  }
  for(unsigned trunc=1;trunc<32;++trunc){
    cp_usb_parser_t parser={};unsigned found=0;
    for(unsigned i=0;i<trunc;++i)cp_usb_feed(&parser,frame[i],&kind,out);
    for(auto byte:frame)found+=cp_usb_feed(&parser,byte,&kind,out);
    assert(found==1&&!memcmp(data,out,20));
  }
  for(unsigned bit=0;bit<32*8;++bit){
    auto parser=cp_usb_parser_t{};frame[bit/8]^=1u<<(bit%8);
    for(auto byte:frame)assert(!cp_usb_feed(&parser,byte,&kind,out));
    frame[bit/8]^=1u<<(bit%8);unsigned found=0;
    for(auto byte:frame)found+=cp_usb_feed(&parser,byte,&kind,out);
    assert(found==1);
  }
  std::cout<<"PASS shared USB framing, corruption/truncation recovery, feedback sessions, volume prediction, text, navigation and stale-state expiry\n";
}
