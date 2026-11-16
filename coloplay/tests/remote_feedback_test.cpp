#include "remote_feedback.h"
#include "coloplay_usb_frame.h"
#include <cassert>
#include <iostream>
int main(){
  RemoteFeedback peer;peer.state.connected=peer.state.wired=true;
  uint8_t data[CP_BLE_BYTES];cp_header(data,CP_HELLO,0,7);data[8]=64;data[16]=CP_CAP_VOLUME_FLOAT|CP_CAP_VOLUME_PREDICT|CP_CAP_VOLUME_SCALE|CP_CAP_NAVIGATION;
  assert(!peer.accept(data,20,8,10));assert(peer.accept(data,20,7,10));
  cp_header(data,CP_STATE,0,7);data[9]=1;data[10]=0x80;cp_put32(data+12,1000);cp_put32(data+16,10000);
  assert(peer.accept(data,20,7,100));
  cp_header(data,CP_VOLUME_STATE,0,7);cp_put_float(data+8,.25f);data[13]=64;cp_put_float(data+16,.25f);
  assert(peer.accept(data,20,7,100));assert(peer.snapshot(200).fresh&&peer.snapshot(200).positionMs==1100);
  assert(peer.snapshot(200).predictionReady&&peer.snapshot(200).displayVolume==.5f);
  assert(peer.prediction.queued(1,1,200));assert(peer.snapshot(210).displayVolume==33.0f/64);
  cp_put16(data+2,1);data[12]=CP_APPLIED;cp_put16(data+14,1);cp_put_float(data+8,(33.0f/64)*(33.0f/64));cp_put_float(data+16,(33.0f/64)*(33.0f/64));
  assert(peer.accept(data,20,7,220));assert(!peer.snapshot(220).volumePending);
  cp_header(data,CP_TRACK,0,7);cp_put32(data+8,123);assert(peer.accept(data,20,7,230));
  cp_header(data,CP_TEXT,0,7);data[2]=0x81;data[8]=5;memcpy(data+9,"Title",5);
  assert(peer.accept(data,20,7,240));assert(!strcmp(peer.snapshot(240).title,"Title"));
  data[3]=62;data[8]=2;assert(!peer.accept(data,20,7,240));
  cp_header(data,CP_NAVIGATION,1,7);data[8]=1;data[9]=CP_NAV_FOCUS;cp_put16(data+18,5);
  assert(peer.accept(data,20,7,250));cp_header(data,CP_NAV_TEXT,1,7);data[9]=5;memcpy(data+10,"Focus",5);
  assert(peer.accept(data,20,7,250));assert(!strcmp(peer.snapshot(260).navigation.ui.focus,"Focus"));
  assert(!peer.snapshot(6000).fresh&&!peer.snapshot(6000).navigation.fresh);
  peer.clear();assert(!peer.snapshot(6000).connected&&!peer.snapshot(6000).predictionReady);

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
