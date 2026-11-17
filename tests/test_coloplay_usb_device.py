"""Actual ColoPlay controller router, with serial and BLE boundaries faked."""
from pathlib import Path
import re,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
PREFIX=r"""
#include "remote_feedback.h"
#include "connection_status.h"
#include "coloplay_usb_frame.h"
#include <string>
#include <vector>
#include <cassert>
#include <cstdio>
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
static uint32_t now=100,random_value=500;
static uint32_t millis(){return now;}
static uint32_t esp_random(){return ++random_value;}
class String:public std::string {
 public:using std::string::string;
 String(const std::string &s):std::string(s){}
 String(uint32_t value):std::string(std::to_string(value)){}
 String(int value):std::string(std::to_string(value)){}
 String(float value,int digits){char b[64];snprintf(b,sizeof(b),"%.*f",digits,double(value));assign(b);}
};
namespace DeviceLog {static void println(const char *){}}
static bool cable=true;static unsigned ble_sent,ble_resets;
static std::vector<std::vector<uint8_t>> frames;
namespace UsbConsole {
 bool connected(){return cable;}
 bool writeFrame(const uint8_t *p,size_t n){frames.emplace_back(p,p+n);return true;}
}
namespace BluetoothService {
 using Snapshot=RemoteFeedback::Snapshot;
 static Snapshot ble;
 Snapshot snapshot(){return ble;}
 void tick(bool){}
 void resetInputs(){++ble_resets;}
 bool send(uint8_t,uint16_t,int,uint16_t=0,uint16_t=0){++ble_sent;return true;}
 void cancel(){}
}
namespace ControllerService {using Snapshot=RemoteFeedback::Snapshot;bool send(uint8_t,uint16_t,int,uint16_t=0,uint16_t=0);}
"""
SUFFIX=r"""
static void deliver(uint8_t kind,const uint8_t *payload){
 uint8_t frame[32];cp_usb_encode(frame,kind,payload);
 for(auto byte:frame)ControllerService::consume(byte);
}
int main(){
 using namespace ControllerService;
 BluetoothService::ble.enabled=BluetoothService::ble.ready=BluetoothService::ble.connected=BluetoothService::ble.secured=true;
 assert(!connections().usb&&connections().bluetooth&&!connections().mpd);
 tick(true);assert(send(CP_KEY,0x52,1,0,0)&&ble_sent==1);
 uint8_t data[20];cp_header(data,CP_HELLO,0,7);deliver(CP_USB_PROBE,data);
 assert(frames.size()==1&&!snapshot().wired);
 cp_usb_parser_t parser={};uint8_t kind,offer[20];bool found=false;
 for(auto b:frames.back())found|=cp_usb_feed(&parser,b,&kind,offer);
 assert(found&&kind==CP_USB_OFFER&&cp_u32(offer+9)==7&&(offer[17]&CP_FEATURE_STAR));
 const uint32_t session=cp_u32(offer+4);
 cp_header(data,CP_HELLO,0,session);data[8]=64;data[16]=CP_CAP_VOLUME_FLOAT|CP_CAP_VOLUME_PREDICT|CP_CAP_VOLUME_SCALE|CP_CAP_NAVIGATION;
 data[17]=CP_FEATURE_STAR;deliver(CP_USB_HELLO,data);tick(true);assert(snapshot().wired&&ble_resets==1);
 assert(connections().usb&&connections().bluetooth&&!connections().mpd); // Handshake alone does not confirm MPD.
 cp_header(data,CP_STATE,0,session);data[9]=1;data[10]=128;cp_put32(data+12,1000);cp_put32(data+16,10000);deliver(CP_USB_DATA,data);
 cp_header(data,CP_VOLUME_STATE,0,session);cp_put_float(data+8,.25f);data[13]=64;cp_put_float(data+16,.25f);deliver(CP_USB_DATA,data);
 assert(snapshot().fresh&&snapshot().predictionReady&&snapshot().displayVolume==.5f);
 assert(connections().usb&&connections().bluetooth&&connections().mpd);
 uint8_t status[20];
 cp_header(status,CP_STATE,0,session);status[9]=0;status[10]=0;deliver(CP_USB_DATA,status);
 assert(connections().usb&&connections().bluetooth&&!connections().mpd); // MPD loss keeps the transports connected.
 status[10]=128;deliver(CP_USB_DATA,status);
 BluetoothService::ble.secured=false;assert(connections().usb&&!connections().bluetooth&&connections().mpd);
 BluetoothService::ble.secured=true;
 // A brief raw CDC/SOF indication is not a protocol disconnect.
 cable=false;now+=10;tick(true);assert(snapshot().wired);cable=true;
 assert(send(CP_VOLUME,233,1,0,0)&&ble_sent==1&&snapshot().volumePending==1);
 const uint16_t request=snapshot().request;
 data[12]=CP_APPLIED;cp_put16(data+2,request);cp_put16(data+14,request);cp_put_float(data+8,(33.0f/64)*(33.0f/64));cp_put_float(data+16,(33.0f/64)*(33.0f/64));deliver(CP_USB_DATA,data);
 assert(snapshot().volumePending==0&&snapshot().result==CP_APPLIED);
 cp_header(data,CP_TRACK,0,session);cp_put32(data+8,123);deliver(CP_USB_DATA,data);
 cp_header(data,CP_STATE,0,session);data[9]=1;data[10]=cp_state_flags(true,123,true,0,true);deliver(CP_USB_DATA,data);
 assert(snapshot().starSupported&&snapshot().starKnown&&snapshot().starred);
 data[10]=cp_state_flags(true,123,false,0,true);deliver(CP_USB_DATA,data);
 assert(snapshot().starKnown&&!snapshot().starred);
 assert(!send(CP_SLEEP,CP_SLEEP_OFF,1,0,0)); // No negotiation yet.
 cp_header(data,CP_HELLO,0,session);data[8]=64;data[16]=CP_CAP_VOLUME_FLOAT;data[17]=CP_FEATURE_SLEEP;
 deliver(CP_USB_HELLO,data);assert(!send(CP_SLEEP,CP_SLEEP_OFF,1,0,0)); // Wait for initial sleep snapshot.
 cp_sleep_t sleep={CP_SLEEP_ON|CP_SLEEP_COUNTDOWN,30,5000};cp_sleep_packet(data,session,&sleep);deliver(CP_USB_DATA,data);
 assert(snapshot().sleep.fresh&&snapshot().sleep.minutes==30&&statusJson().find("remaining_ms")!=std::string::npos);
 assert(send(CP_KEY,CP_KEY_SCREEN_OFF,7,0,0));
 parser={};for(auto b:frames.back())cp_usb_feed(&parser,b,&kind,offer);
 assert(kind==CP_USB_DATA&&offer[1]==CP_SLEEP&&cp_u16(offer+8)==CP_SLEEP_OFF&&cp_u16(offer+10)==1&&cp_command_valid(offer,20));
 assert(!send(CP_KEY,CP_KEY_SCREEN_OFF,1,1,0));
 assert(send(CP_SLEEP,CP_SLEEP_OFF,1,0,0));
 cp_sleep_packet(data,session+1,&sleep);deliver(CP_USB_DATA,data);assert(snapshot().sleep.fresh);
 // A new host challenge discards prediction and creates a new nonce, even if DTR stays asserted.
 cp_header(data,CP_HELLO,0,8);now+=300;deliver(CP_USB_PROBE,data);assert(!snapshot().wired);
 auto &wire=frames.back();parser={};for(auto b:wire)cp_usb_feed(&parser,b,&kind,offer);
 assert(cp_u32(offer+4)!=session&&cp_u32(offer+9)==8);
 const uint32_t replacement=cp_u32(offer+4);
 cp_header(data,CP_HELLO,0,replacement);data[8]=64;data[16]=CP_CAP_VOLUME_FLOAT|CP_CAP_VOLUME_SCALE|CP_CAP_VOLUME_PREDICT;
 deliver(CP_USB_HELLO,data);assert(snapshot().wired&&!snapshot().volumePending);
 // Replayed state from the previous cable session must be ignored.
 cp_header(data,CP_STATE,0,session);data[9]=1;data[10]=128;deliver(CP_USB_DATA,data);assert(!snapshot().fresh);
 cp_header(data,CP_STATE,0,replacement);data[9]=0;data[10]=128;deliver(CP_USB_DATA,data);
 assert(connections().usb&&connections().mpd); // Stopped playback is still an MPD connection.
 now+=PlaybackClock::kFreshMs+1;deliver(CP_USB_PING,data);tick(true);
 assert(connections().usb&&!connections().mpd); // Serial heartbeats do not make stale MPD state current.
 BluetoothService::ble.fresh=BluetoothService::ble.available=true;
 now+=1600;tick(true);assert(!snapshot().wired);
 assert(!connections().usb&&connections().bluetooth&&connections().mpd);assert(send(CP_KEY,0x52,1,0,0)&&ble_sent==2);
 assert(statusJson().find("ble")!=std::string::npos);
 puts("PASS device USB handshake, fresh feedback, volume prediction, transport routing, challenge reset, stale frames and BLE fallback");
}
"""
class ColoPlayUsbDeviceTests(unittest.TestCase):
 def test_production_router(self):
  with tempfile.TemporaryDirectory() as tmp:
   path=Path(tmp);source=(ROOT/'coloplay/firmware/src/controller_service.cpp').read_text();source=re.sub(r'^#include .*$', '',source,flags=re.M)
   (path/'device.cpp').write_text(PREFIX+source+SUFFIX)
   binary=path/'device'
   subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'coloplay/firmware/include'),str(path/'device.cpp'),'-o',str(binary)],check=True)
   subprocess.run([str(binary)],check=True)
if __name__=='__main__':unittest.main()
