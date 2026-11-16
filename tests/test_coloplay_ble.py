"""Production ColoPlay GATT peer driver against asynchronous GATT/RTOS boundaries."""
from pathlib import Path
import re,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
PREFIX=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>
#include "coloplay_ble_protocol.h"
#include "volume_grid.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define BLE_UUID_TYPE_16 16
#define BLE_UUID_TYPE_128 128
#define BLE_HS_EDONE 14
#define ESP_OK 0
#define ESP_ERR_NO_MEM 257
#define BLE_UUID128_INIT(...) { .u={128}, .value={__VA_ARGS__} }
typedef int esp_err_t;
typedef struct {int type;} ble_uuid_t;
typedef struct {ble_uuid_t u;uint16_t value;} ble_uuid16_t;
typedef struct {ble_uuid_t u;uint8_t value[16];} ble_uuid128_t;
typedef union {ble_uuid_t u;ble_uuid16_t u16;ble_uuid128_t u128;} ble_uuid_any_t;
struct ble_gatt_error {int status;};
struct ble_gatt_chr {ble_uuid_any_t uuid;uint16_t val_handle,def_handle;};
struct ble_gatt_dsc {ble_uuid_any_t uuid;uint16_t handle;};
struct fake_mbuf {unsigned len;uint8_t bytes[20];};
struct ble_gatt_attr {struct fake_mbuf *om;};
struct ble_gap_upd_params {int itvl_min,itvl_max,latency,supervision_timeout;};
static int ble_uuid_cmp(const ble_uuid_t *a,const ble_uuid_t *b){return a->type!=b->type?1:memcmp(((const ble_uuid128_t*)a)->value,((const ble_uuid128_t*)b)->value,16);}
#define OS_MBUF_PKTLEN(m) ((m)->len)
static int os_mbuf_copydata(struct fake_mbuf *m,int offset,int len,void *out){memcpy(out,m->bytes+offset,len);return 0;}
static int64_t clock_us=1000000;
static int64_t esp_timer_get_time(void){return clock_us;}
static int ble_gap_update_params(int conn,struct ble_gap_upd_params *p){(void)conn;assert(p->itvl_min==6&&p->itvl_max==12&&p->latency==0);return 0;}
static bool mpd_client_coloplay_accepts(uint32_t session){return session!=0;}
static int native_steps=64;
static int player_volume_steps(void){return native_steps;}
typedef struct {bool connected,starred;float volume,volume_grid_target;uint8_t volume_grid_steps;uint16_t volume_request;int play_status;uint32_t elapsed_ms,duration_ms,revision;uint64_t track_key;char title[128],artist[128];} mpd_state_t;
static float remote_volume=.67f;static bool float_peer,predict_peer,scale_peer,navigation_peer,star_peer,remote_star;
static bool remote_connected=true;static uint64_t remote_track=123;
static float remote_target=.25f;static uint16_t remote_completed=7;
static uint32_t backend_session;static unsigned commands,releases,key_events;static int backend_error;
static bool mpd_client_get_remote_state(mpd_state_t *s){memset(s,0,sizeof(*s));s->connected=remote_connected;s->starred=remote_star;s->volume=remote_volume;s->volume_grid_target=remote_target;s->volume_grid_steps=native_steps;s->volume_request=remote_completed;s->play_status=1;s->track_key=remote_track;s->elapsed_ms=1000;s->duration_ms=100000;strcpy(s->title,"A real title");strcpy(s->artist,"Artist");return true;}
static void mpd_client_coloplay_begin(uint32_t session){backend_session=session;}
static void mpd_client_coloplay_release(void){backend_session=0;releases++;}
static uint8_t mpd_client_coloplay_seek_state(void){return 0;}
static int mpd_client_coloplay_command(const uint8_t *p){assert(cp_command_valid(p,20));commands++;return backend_error;}
typedef enum {UI_KEY_UP,UI_KEY_DOWN,UI_KEY_ENTER,UI_KEY_BACK,UI_KEY_PAGE_UP,UI_KEY_PAGE_DOWN,UI_KEY_SCREEN_PREV,UI_KEY_SCREEN_NEXT,UI_KEY_CONTEXT,UI_KEY_STAR,UI_KEY_1=20} ui_key_t;
static ui_key_t last_key;
static void usb_hid_input_post_key(ui_key_t k){last_key=k;key_events++;}
struct ble_npl_event {void(*callback)(struct ble_npl_event*);};
static void ble_npl_event_init(struct ble_npl_event *e,void(*callback)(struct ble_npl_event*),void *arg){(void)arg;e->callback=callback;}
static void *nimble_port_get_dflt_eventq(void){return NULL;}
static struct ble_npl_event *last_event;static unsigned event_wakes;
static void ble_npl_eventq_put(void *queue,struct ble_npl_event *event){(void)queue;assert(event->callback);last_event=event;event_wakes++;}
#define pdTRUE 1
typedef cp_navigation_t ui_navigation_t;
typedef struct {unsigned cap,size,read,count;uint8_t bytes[32][128];} *QueueHandle_t;
static QueueHandle_t xQueueCreate(unsigned cap,unsigned size){assert(cap<=32&&size<=128);QueueHandle_t q=calloc(1,sizeof(*q));q->cap=cap;q->size=size;return q;}
static void xQueueReset(QueueHandle_t q){q->read=q->count=0;}
static int xQueueSend(QueueHandle_t q,const void *data,int wait){(void)wait;if(q->count==q->cap)return 0;memcpy(q->bytes[(q->read+q->count)%q->cap],data,q->size);q->count++;return 1;}
static int xQueueOverwrite(QueueHandle_t q,const void *data){assert(q->cap==1);memcpy(q->bytes[0],data,q->size);q->read=0;q->count=1;return 1;}
static int xQueuePeek(QueueHandle_t q,void *data,int wait){(void)wait;if(!q->count)return 0;memcpy(data,q->bytes[q->read],q->size);return 1;}
static int xQueueReceive(QueueHandle_t q,void *data,int wait){(void)wait;if(!q->count)return 0;memcpy(data,q->bytes[q->read],q->size);q->read=(q->read+1)%q->cap;q->count--;return 1;}
static unsigned navigation_frames,navigation_used;static uint16_t navigation_revision;static char navigation_text[96];static uint8_t navigation_header[20];
static int writes;static uint8_t last_write[20],last_volume[20],last_hello[20];
static int (*write_cb)(uint16_t,const struct ble_gatt_error*,struct ble_gatt_attr*,void*);static void *write_arg;
static int ble_gattc_write_flat(uint16_t c,uint16_t h,const void *data,unsigned len,int(*cb)(uint16_t,const struct ble_gatt_error*,struct ble_gatt_attr*,void*),void *arg){(void)c;(void)h;assert(len==2||len==20);memcpy(last_write,data,len);if(len==20&&((const uint8_t*)data)[1]==CP_VOLUME_STATE)memcpy(last_volume,data,20);if(len==20&&((const uint8_t*)data)[1]==CP_HELLO)memcpy(last_hello,data,20);if(len==20&&((const uint8_t*)data)[1]==CP_NAVIGATION){const uint8_t *p=data;navigation_frames++;if(cp_u16(p+2)!=navigation_revision){navigation_revision=cp_u16(p+2);navigation_used=0;memset(navigation_text,0,sizeof(navigation_text));}memcpy(navigation_header,p,20);}
if(len==20&&((const uint8_t*)data)[1]==CP_NAV_TEXT){const uint8_t *p=data;assert(cp_u16(p+2)==navigation_revision&&p[8]==navigation_used&&p[9]<=10);memcpy(navigation_text+p[8],p+10,p[9]);navigation_used+=p[9];}
writes++;write_cb=cb;write_arg=arg;return 0;}
#define ble_gattc_read(...) 0
#define ble_gattc_disc_all_dscs(...) 0
#define ble_gattc_disc_all_chrs(...) 0
'''
SUFFIX=r'''
static unsigned ready_calls,failed_calls;
static void ready(void){ready_calls++;}
static void failed(const char *reason){assert(reason);failed_calls++;}
static void ack(void){if(write_cb){struct ble_gatt_error ok={0};void *a=write_arg;int(*cb)(uint16_t,const struct ble_gatt_error*,struct ble_gatt_attr*,void*)=write_cb;write_cb=NULL;cb(s_conn,&ok,NULL,a);}}
static void drain(void){for(unsigned n=0;n<100&&write_cb;n++)ack();assert(!write_cb);}
static void connect_peer(uint32_t session,bool reordered){
 coloplay_ble_reset();coloplay_ble_service(&s_service.u,10,20);assert(coloplay_ble_discover(7,ready,failed));
 struct ble_gatt_error ok={0},done={BLE_HS_EDONE};struct ble_gatt_chr chr={0};
 chr.uuid.u128=s_service;chr.uuid.u128.value[12]=reordered?4:2;chr.val_handle=11;chr.def_handle=10;characteristic(7,&ok,&chr,generation());
 chr.uuid.u128.value[12]=reordered?2:3;chr.val_handle=14;chr.def_handle=13;characteristic(7,&ok,&chr,generation());
 chr.uuid.u128.value[12]=reordered?3:4;chr.val_handle=18;chr.def_handle=17;characteristic(7,&ok,&chr,generation());characteristic(7,&done,NULL,generation());
 struct fake_mbuf mb={.len=20};cp_header(mb.bytes,CP_HELLO,0,session);mb.bytes[8]=(float_peer?CP_CAP_VOLUME_FLOAT:0)|(predict_peer?CP_CAP_VOLUME_PREDICT:0)|(scale_peer?CP_CAP_VOLUME_SCALE:0)|(navigation_peer?CP_CAP_NAVIGATION:0);mb.bytes[17]=star_peer?CP_FEATURE_STAR:0;struct ble_gatt_attr attr={&mb};caps_read(7,&ok,&attr,generation());
 struct ble_gatt_dsc d={.uuid.u16={{BLE_UUID_TYPE_16},0x2902},.handle=(uint16_t)(s_command+1)};descriptor(7,&ok,12,&d,generation());descriptor(7,&done,12,NULL,generation());ack();
 assert(last_write[1]==CP_HELLO&&last_write[8]==native_steps&&cp_u32(last_write+4)==session);assert((last_write[16]&CP_CAP_VOLUME_FLOAT)==(float_peer?CP_CAP_VOLUME_FLOAT:0));ack();assert(coloplay_ble_connected()&&backend_session==session);
 coloplay_ble_tick(clock_us);drain();
}
int main(void){
 connect_peer(123,false);assert(ready_calls==1&&!failed_calls);
 uint8_t request[20];cp_header(request,CP_VOLUME,1,123);cp_put16(request+8,233);cp_put16(request+10,1);cp_put16(request+12,64);
 assert(coloplay_ble_notification(7,s_command,request,20));assert(commands==1&&last_write[1]==CP_STATE&&last_write[8]==67&&last_write[11]==CP_ACCEPTED);drain();
 coloplay_ble_notification(7,s_command,request,20);assert(commands==1);drain();
 coloplay_ble_result(request,CP_APPLIED);coloplay_ble_tick(clock_us);assert(last_write[11]==CP_APPLIED);drain();
 cp_put32(request+4,999);coloplay_ble_notification(7,s_command,request,20);assert(commands==1);
 cp_header(request,CP_KEY,2,123);cp_put16(request+8,0x4b);cp_put16(request+10,1);coloplay_ble_notification(7,s_command,request,20);assert(key_events==1);drain();
 cp_put16(request+2,3);cp_put16(request+12,1);coloplay_ble_notification(7,s_command,request,20);assert(key_events==1&&last_write[11]==CP_UNSUPPORTED);drain();
 // '*' toggles the existing server-backed star action, with no context-menu steps.
 cp_header(request,CP_KEY,4,123);cp_put16(request+8,0x55);cp_put16(request+10,1);
 coloplay_ble_notification(7,s_command,request,20);assert(key_events==2&&last_key==UI_KEY_STAR&&last_write[11]==CP_APPLIED);drain();
 coloplay_ble_notification(7,s_command,request,20);assert(key_events==2);drain();
 cp_put16(request+2,5);cp_put16(request+8,0x25);cp_put16(request+12,0x02);
 coloplay_ble_notification(7,s_command,request,20);assert(key_events==3&&last_key==UI_KEY_STAR);drain();
 cp_put16(request+2,6);cp_put16(request+12,0x20);
 coloplay_ble_notification(7,s_command,request,20);assert(key_events==4&&last_key==UI_KEY_STAR);drain();
 cp_put16(request+2,7);cp_put16(request+12,0);
 coloplay_ble_notification(7,s_command,request,20);assert(key_events==4&&last_write[11]==CP_UNSUPPORTED);drain();
 cp_put16(request+2,8);cp_put16(request+12,0x03);
 coloplay_ble_notification(7,s_command,request,20);assert(key_events==4&&last_write[11]==CP_UNSUPPORTED);drain();
 cp_put16(request+2,9);cp_put16(request+8,0x55);cp_put16(request+12,0x02);
 coloplay_ble_notification(7,s_command,request,20);assert(key_events==4&&last_write[11]==CP_UNSUPPORTED);drain();
 cp_put16(request+2,10);cp_put16(request+8,0x06);cp_put16(request+12,0);
 coloplay_ble_notification(7,s_command,request,20);assert(key_events==5&&last_key==UI_KEY_CONTEXT);drain();
 // Delayed write callbacks and MPD results from the old session cannot mutate a new connection.
 uint32_t old_generation=s_generation;connect_peer(456,true);unsigned before=writes;struct ble_gatt_error bad={99};wrote(7,&bad,NULL,(void*)(uintptr_t)old_generation);assert(writes==before&&!failed_calls);
 cp_header(request,CP_VOLUME,1,123);cp_put16(request+8,233);cp_put16(request+10,1);cp_put16(request+12,64);coloplay_ble_result(request,CP_APPLIED);coloplay_ble_tick(clock_us);drain();assert(coloplay_ble_connected());
 cp_put32(request+4,456);backend_error=ESP_ERR_NO_MEM;coloplay_ble_notification(7,s_command,request,20);assert(last_write[11]==CP_BUSY);drain();
 coloplay_ble_reset();assert(!coloplay_ble_connected()&&backend_session==0&&releases>=3);
 float_peer=true;backend_error=0;remote_volume=1.0f/64;connect_peer(789,false);
 cp_header(request,CP_VOLUME,1,789);cp_put16(request+8,233);cp_put16(request+10,1);cp_put16(request+12,64);cp_put_float(request+16,1.0f/64);
 coloplay_ble_notification(7,s_command,request,20);drain();
 assert(last_write[1]==CP_VOLUME_STATE&&cp_float(last_write+8)==1.0f/64&&last_write[12]==CP_ACCEPTED);
 remote_volume=2.0f/64;coloplay_ble_result(request,CP_APPLIED);coloplay_ble_tick(clock_us);drain();
 assert(last_write[1]==CP_VOLUME_STATE&&cp_float(last_write+8)==2.0f/64&&last_write[12]==CP_APPLIED);
 unsigned applied=commands;coloplay_ble_notification(7,s_command,request,20);drain();assert(commands==applied&&cp_float(last_write+8)==2.0f/64);
 coloplay_ble_reset();
 predict_peer=true;scale_peer=false;connect_peer(998,false);assert(!s_prediction);coloplay_ble_reset();
 scale_peer=true;predict_peer=true;remote_volume=.253953f;remote_target=.25f;remote_completed=7;connect_peer(999,false);
 assert(s_prediction);
 cp_header(request,CP_VOLUME,8,999);cp_put16(request+8,233);cp_put16(request+10,1);cp_put16(request+12,1);
 coloplay_ble_notification(7,s_command,request,20);drain();
 assert(last_write[1]==CP_VOLUME_STATE&&last_write[13]==64&&cp_float(last_write+16)==.25f&&cp_u16(last_write+14)==7);
 remote_volume=.261951f;remote_target=17.0f/64;remote_completed=8;
 coloplay_ble_result(request,CP_APPLIED);coloplay_ble_tick(clock_us);drain();
 assert(cp_u16(last_write+14)==8&&cp_float(last_write+16)==17.0f/64);
 native_steps=16;remote_target=-1;clock_us+=500000;coloplay_ble_tick(clock_us);drain();
 assert(last_volume[13]==16&&cp_float(last_volume+16)==-1);
 assert(last_hello[8]==16&&(last_hello[16]&CP_CAP_VOLUME_PREDICT));
 for(int mode=2;mode<=5;mode+=3){native_steps=mode;clock_us+=500000;coloplay_ble_tick(clock_us);drain();
   assert(last_volume[13]==mode&&last_hello[8]==mode&&(last_hello[16]&CP_CAP_VOLUME_SCALE));
   assert(cp_float(last_hello+12)==1.0f/volume_grid_count(mode));
 }
 coloplay_ble_reset();
 // Old peers receive no navigation frames. A negotiated peer gets coherent bounded UTF-8 snapshots.
 ui_navigation_t nav={.view=1,.flags=CP_NAV_FOCUS|CP_NAV_SCREEN_ON,.index=7,.count=200};
 strcpy(nav.focus,"日本語のアルバム 한글 노래");coloplay_ble_publish_navigation(&nav);
 connect_peer(1110,false);for(int n=0;n<15;n++){clock_us+=20000;coloplay_ble_tick(clock_us);drain();}assert(!navigation_frames);coloplay_ble_reset();
 navigation_peer=true;connect_peer(1111,false);
 for(int n=0;n<15;n++){clock_us+=20000;coloplay_ble_tick(clock_us);drain();}
 assert(navigation_frames&&navigation_used==strlen(nav.focus)&&!strcmp(navigation_text,nav.focus));
 assert(navigation_header[8]==1&&cp_u32(navigation_header+10)==7&&cp_u32(navigation_header+14)==200);
 assert(last_hello[16]&CP_CAP_NAVIGATION);
 nav.view=3;nav.index=21;strcpy(nav.focus,"Newest focus");unsigned wakes=event_wakes;coloplay_ble_publish_navigation(&nav);
 assert(event_wakes==wakes+1&&last_event==&s_navigation_event);last_event->callback(last_event);drain();
 coloplay_ble_publish_navigation(&nav);assert(event_wakes==wakes+1);
 assert(navigation_header[8]==3&&cp_u32(navigation_header+10)==21&&!strcmp(navigation_text,"Newest focus"));
 // Backpressure cannot strand a changed focus until the housekeeping timer.
 queue_state(0,0);for(int n=0;n<6;n++)assert(queue_state(0,0));assert(s_count>=4);
 strcpy(nav.focus,"Superseded");coloplay_ble_publish_navigation(&nav);
 memset(nav.focus,'x',95);nav.focus[95]=0;coloplay_ble_publish_navigation(&nav);
 last_event->callback(last_event);drain();assert(navigation_used==95&&!strcmp(navigation_text,nav.focus));
 unsigned frames=navigation_frames;clock_us+=1100000;coloplay_ble_tick(clock_us);drain();assert(navigation_frames==frames+1&&!strcmp(navigation_text,nav.focus));
 nav.flags=CP_NAV_LOADING;nav.index=nav.count=0;nav.focus[0]=0;coloplay_ble_publish_navigation(&nav);clock_us+=20000;coloplay_ble_tick(clock_us);drain();assert(navigation_header[9]==CP_NAV_LOADING&&!navigation_used);
 coloplay_ble_reset();
 // Star flags are negotiated and reflect confirmed current-track state only.
 uint8_t status[20];remote_star=true;connect_peer(2220,false);
 state_packet(status,0,0);assert(!(status[10]&(CP_STATE_STAR_KNOWN|CP_STATE_STARRED)));
 star_peer=true;connect_peer(2221,false);assert(last_hello[17]&CP_FEATURE_STAR);
 state_packet(status,0,0);assert(cp_state_valid(status,20)&&(status[10]&CP_STATE_STAR_KNOWN)&&(status[10]&CP_STATE_STARRED));
 remote_star=false;state_packet(status,0,0);assert((status[10]&CP_STATE_STAR_KNOWN)&&!(status[10]&CP_STATE_STARRED));
 remote_track=0;state_packet(status,0,0);assert(!(status[10]&CP_STATE_STAR_KNOWN));
 remote_track=123;remote_connected=false;state_packet(status,0,0);assert(!(status[10]&(CP_STATE_CONNECTED|CP_STATE_STAR_KNOWN)));
 remote_connected=true;star_peer=false;connect_peer(2222,false);assert(!s_star_feedback&&!(last_hello[17]&CP_FEATURE_STAR));
 coloplay_ble_reset();
 puts("ColoPlay GATT discovery/handshake, raw state, duplicates, key mapping, queue errors and stale-session isolation passed");return 0;
}
'''
class ColoPlayBleTests(unittest.TestCase):
 def test_production_driver(self):
  with tempfile.TemporaryDirectory() as tmp:
   path=Path(tmp);source=(ROOT/'firmware/main/coloplay_ble.c').read_text();source=re.sub(r'^#include .*$', '',source,flags=re.M)
   (path/'driver.c').write_text(PREFIX+source+SUFFIX)
   binary=path/'driver';subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wno-unused-function','-I'+str(ROOT/'firmware/main'),'-I'+str(ROOT/'app/backend'),str(path/'driver.c'),'-o',str(binary)],check=True);subprocess.run([str(binary)],check=True)
if __name__=='__main__':unittest.main()
