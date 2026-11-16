#include "controller_service.h"
#include "coloplay_usb_frame.h"
#include "usb_console.h"
#include "device_log.h"
#include <esp_system.h>

namespace ControllerService {
namespace {
portMUX_TYPE gMux=portMUX_INITIALIZER_UNLOCKED;
RemoteFeedback gFeedback;
cp_usb_parser_t gParser={};
uint32_t gChallenge=0,gSession=0,gLastRx=0,gLastOffer=0,gLastPing=0,gSent=0,gDropped=0,gInvalid=0,gRequestAt=0;
uint16_t gRequest=0;
uint32_t gStale=0,gLeaseTimeouts=0,gConnections=0;
bool gConnected=false,gAllowed=false,gWasWired=false;
String quote(const char *value){
  String out="\"";
  for(const unsigned char *p=(const unsigned char*)value;*p;++p){if(*p=='"'||*p=='\\')out+='\\';if(*p>=32)out+=char(*p);}
  return out+'"';
}
bool frame(uint8_t kind,const uint8_t *data){
  uint8_t bytes[CP_USB_FRAME_BYTES];cp_usb_encode(bytes,kind,data);
  if(!UsbConsole::writeFrame(bytes,sizeof(bytes))){++gDropped;return false;}
  ++gSent;return true;
}
void reset(){gFeedback.clear();gConnected=false;gSession=0;gRequest=0;gChallenge=0;gLastOffer=0;}
void offer(uint32_t now){
  if(!gSession){gSession=esp_random()|0x80000000u;gFeedback.clear();}
  uint8_t data[CP_BLE_BYTES];cp_header(data,CP_HELLO,0,gSession);
  cp_put32(data+9,gChallenge);
  data[8]=CP_CAP_MEDIA|CP_CAP_VOLUME|CP_CAP_KEY|CP_CAP_STATE|CP_CAP_VOLUME_FLOAT|CP_CAP_VOLUME_PREDICT|CP_CAP_VOLUME_SCALE|CP_CAP_NAVIGATION;
  if(frame(CP_USB_OFFER,data))gLastOffer=now;
}
void received(uint8_t kind,const uint8_t *data,uint32_t now){
  if(!gAllowed)return;
  if(kind==CP_USB_PROBE){
    if(!cp_packet(data,CP_BLE_BYTES)||data[1]!=CP_HELLO||!cp_u32(data+4)){++gInvalid;return;}
    const uint32_t challenge=cp_u32(data+4);
    if(gChallenge!=challenge){reset();gChallenge=challenge;}
    if(!gLastOffer||uint32_t(now-gLastOffer)>=250)offer(now);
    return;
  }
  if(!cp_packet(data,CP_BLE_BYTES)){++gInvalid;return;}
  if(!gSession||cp_u32(data+4)!=gSession){++gStale;return;}
  if(kind==CP_USB_HELLO){
    if(data[1]!=CP_HELLO||!volume_scale_valid(data[8])||!(data[16]&CP_CAP_VOLUME_FLOAT)){++gInvalid;return;}
    if(gFeedback.accept(data,CP_BLE_BYTES,gSession,now)&&frame(CP_USB_READY,data)){
      if(!gConnected)++gConnections;
      gFeedback.state.wired=gFeedback.state.enabled=gFeedback.state.ready=gFeedback.state.connected=true;
      gLastRx=now;gConnected=true;
    }
    return;
  }
  if(!gConnected)return;
  if(kind==CP_USB_PING){gLastRx=now;return;}
  if(kind==CP_USB_DATA){
    if(gFeedback.accept(data,CP_BLE_BYTES,gSession,now))gLastRx=now;
    else ++gInvalid;
  }
}
}
bool consume(uint8_t byte){
  portENTER_CRITICAL(&gMux);
  const bool claimed=gConnected||gParser.used||byte==0xc7;
  uint8_t kind,data[CP_BLE_BYTES];
  if(cp_usb_feed(&gParser,byte,&kind,data))received(kind,data,millis());
  portEXIT_CRITICAL(&gMux);return claimed;
}
Snapshot snapshot(){
  portENTER_CRITICAL(&gMux);const bool wired=gConnected;
  auto out=gFeedback.snapshot(millis());portEXIT_CRITICAL(&gMux);
  return wired?out:BluetoothService::snapshot();
}
void tick(bool hardwareReady){
  const uint32_t now=millis();
  portENTER_CRITICAL(&gMux);gAllowed=hardwareReady;
  // Arduino Serial/JTAG's SOF-based flag can flicker after a 5 ms bus gap.
  // Only validated protocol traffic determines a live controller's lease.
  if(!hardwareReady)reset();
  else if(gConnected&&uint32_t(now-gLastRx)>1500){++gLeaseTimeouts;reset();}
  if(gConnected){
    if(uint32_t(now-gLastPing)>=250){uint8_t data[CP_BLE_BYTES];cp_header(data,CP_HELLO,0,gSession);if(frame(CP_USB_PING,data))gLastPing=now;}
    gFeedback.prediction.expire(now);
    if(gFeedback.state.pending&&uint32_t(now-gRequestAt)>5000){gFeedback.state.pending=false;gFeedback.state.result=CP_FAILED;}
  }
  const bool wired=gConnected;portEXIT_CRITICAL(&gMux);
  if(wired!=gWasWired){
    BluetoothService::resetInputs();gWasWired=wired;
    DeviceLog::println(wired?"[controller] USB CDC-ACM active":"[controller] USB disconnected; using Bluetooth when available");
  }
}
bool send(uint8_t kind,uint16_t usage,int count,uint16_t parameter,uint16_t duration){
  portENTER_CRITICAL(&gMux);
  if(!gConnected){portEXIT_CRITICAL(&gMux);return BluetoothService::send(kind,usage,count,parameter,duration);}
  if(kind==CP_VOLUME){parameter=1;duration=0;}
  if(count<1||count>(kind==CP_VOLUME?32767:127)||(kind==CP_VOLUME&&!gFeedback.prediction.canQueue())){portEXIT_CRITICAL(&gMux);return false;}
  if(!++gRequest)++gRequest;
  if(kind==CP_VOLUME&&gRequest==gFeedback.prediction.completed()){if(!++gRequest)++gRequest;}
  uint8_t data[CP_BLE_BYTES];cp_header(data,kind,gRequest,gSession);
  cp_put16(data+8,usage);cp_put16(data+10,count);cp_put16(data+12,parameter);cp_put16(data+14,duration);
  const bool sent=cp_command_valid(data,sizeof(data))&&frame(CP_USB_DATA,data);
  if(sent){
    gRequestAt=millis();gFeedback.state.request=gRequest;gFeedback.state.usage=usage;gFeedback.state.result=CP_PENDING;gFeedback.state.pending=true;
    if(kind==CP_VOLUME)gFeedback.prediction.queued(gRequest,(usage==0xe9?1:-1)*count,millis());
  }
  portEXIT_CRITICAL(&gMux);return sent;
}
void cancel(){
  portENTER_CRITICAL(&gMux);const bool wired=gConnected;gFeedback.prediction.invalidate();portEXIT_CRITICAL(&gMux);
  if(wired)send(CP_RELEASE,0,1);else BluetoothService::cancel();
}
String statusJson(){
  const auto s=snapshot();
  portENTER_CRITICAL(&gMux);const uint32_t session=gSession,sent=gSent,dropped=gDropped,invalid=gInvalid+gParser.invalid,stale=gStale,timeouts=gLeaseTimeouts,connections=gConnections;const bool wired=gConnected;portEXIT_CRITICAL(&gMux);
  String out="{\"transport\":\""+String(wired?"usb_cdc_acm":s.connected?"ble":"none")+"\",\"usb_connected\":"+String(wired?"true":"false");
  out+=",\"session\":"+String(session)+",\"fresh\":"+String(s.fresh?"true":"false")+",\"available\":"+String(s.available?"true":"false");
  out+=",\"volume_scale\":\""+String(volume_scale_name(s.volumeSteps))+"\",\"volume_fraction\":"+String(s.volume,8)+",\"display_volume_fraction\":"+String(s.displayVolume,8);
  out+=",\"request\":"+String(s.request)+",\"result\":"+String(s.result)+",\"volume_pending\":"+String(s.volumePending)+",\"sent\":"+String(sent)+",\"dropped\":"+String(dropped)+",\"invalid\":"+String(invalid);
  out+=",\"stale_frames\":"+String(stale)+",\"lease_timeouts\":"+String(timeouts)+",\"usb_connections\":"+String(connections);
  out+=",\"playback\":"+String(s.playback)+",\"seek\":"+String(s.seek)+",\"title\":"+quote(s.title)+",\"artist\":"+quote(s.artist);
  out+=",\"position_ms\":"+String(s.positionMs)+",\"duration_ms\":"+String(s.durationMs)+",\"position_valid\":"+String(s.positionValid?"true":"false");
  out+=",\"navigation\":{\"fresh\":"+String(s.navigation.fresh?"true":"false")+",\"view_id\":"+String(s.navigation.ui.view)+",\"flags\":"+String(s.navigation.ui.flags);
  out+=",\"index\":"+String(s.navigation.ui.index)+",\"count\":"+String(s.navigation.ui.count)+",\"focus\":"+quote(s.navigation.ui.focus)+"}}";return out;
}
}
