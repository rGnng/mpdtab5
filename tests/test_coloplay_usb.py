"""Exercise the actual USB host driver against CDC/RTOS boundaries."""
from pathlib import Path
import re,subprocess,tempfile,unittest
from test_coloplay_ble import PREFIX as BLE_PREFIX
ROOT=Path(__file__).resolve().parents[1]
PREFIX=BLE_PREFIX.split('static unsigned navigation_frames')[0].replace('cap<=32','cap<=64').replace('bytes[32][128]','bytes[64][128]')
PREFIX += r'''
#include "coloplay_usb_frame.h"
#define ESP_LOGE(...) ((void)0)
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
#define vTaskDelay(n) ((void)(n))
#define xTaskCreateWithCaps(...) 1
static void mpd_client_coloplay_usb_begin(uint32_t session){backend_session=session;}
static void mpd_client_coloplay_usb_release(void){backend_session=0;releases++;}
typedef void *cdc_acm_dev_hdl_t;
typedef void *usb_device_handle_t;
typedef struct {uint16_t idVendor,idProduct;} usb_device_desc_t;
static usb_device_desc_t desc={0x303a,0x1001};
static int usb_host_get_device_descriptor(usb_device_handle_t d,const usb_device_desc_t **out){(void)d;*out=&desc;return 0;}
typedef struct {int type;} cdc_acm_host_dev_event_data_t;
enum {CDC_ACM_HOST_DEVICE_DISCONNECTED,CDC_ACM_HOST_ERROR};
typedef struct {unsigned connection_timeout_ms,out_buffer_size,in_buffer_size;void *user_arg;void(*event_cb)(const cdc_acm_host_dev_event_data_t*,void*);bool(*data_cb)(const uint8_t*,size_t,void*);} cdc_acm_host_device_config_t;
typedef struct {unsigned driver_task_stack_size,driver_task_priority;int xCoreID;void(*new_dev_cb)(void*);} cdc_acm_host_driver_config_t;
static int cdc_acm_host_install(const cdc_acm_host_driver_config_t *c){assert(c->new_dev_cb);return 0;}
static int cdc_acm_host_open(int vid,int pid,int itf,const cdc_acm_host_device_config_t *cfg,void **out){(void)vid;(void)pid;(void)itf;(void)cfg;(void)out;return 1;}
static int cdc_acm_host_set_control_line_state(void *d,bool dtr,bool rts){(void)d;assert(dtr&&!rts);return 0;}
static unsigned closed,tx_count;static uint8_t tx_kind[512],tx_data[512][20];
static int cdc_acm_host_close(void *d){assert(d);closed++;return 0;}
static int cdc_acm_host_data_tx_blocking(void *dev,const uint8_t *data,size_t n,int timeout){
 assert(dev&&n==32&&timeout==50&&tx_count<512);cp_usb_parser_t parser={0};uint8_t kind,out[20];bool found=false;
 for(unsigned i=0;i<n;i++)found|=cp_usb_feed(&parser,data[i],&kind,out);
 assert(found);tx_kind[tx_count]=kind;memcpy(tx_data[tx_count++],out,20);return 0;
}
static uint32_t esp_random(void){return 0x12345678;}
typedef int cJSON;
#define cJSON_AddObjectToObject(...) NULL
#define cJSON_AddBoolToObject(...) ((void)0)
#define cJSON_AddNumberToObject(...) ((void)0)
'''
SUFFIX=r'''
int main(void){
 coloplay_usb_start();s_device=(void*)1;atomic_store(&s_open,true);s_challenge=99;
 frame_t offer={.kind=CP_USB_OFFER};cp_header(offer.bytes,CP_HELLO,0,123);offer.bytes[8]=255;cp_put32(offer.bytes+9,98);
 consume(&offer,100);assert(!s_nonce&&!atomic_load(&s_ready));
 cp_put32(offer.bytes+9,99);consume(&offer,100);assert(s_nonce==123&&!atomic_load(&s_ready)&&!backend_session&&tx_kind[0]==CP_USB_HELLO);
 frame_t ready={.kind=CP_USB_READY};memcpy(ready.bytes,tx_data[0],20);consume(&ready,110);
 assert(atomic_load(&s_ready)&&backend_session==123);consume(&ready,111);assert(backend_session==123);
 frame_t input={.kind=CP_USB_DATA};cp_header(input.bytes,CP_VOLUME,1,123);cp_put16(input.bytes+8,233);cp_put16(input.bytes+10,1);cp_put16(input.bytes+12,1);
 consume(&input,120);assert(commands==1);consume(&input,121);assert(commands==1);
 assert(coloplay_usb_result(input.bytes,CP_APPLIED));result_t result;assert(xQueueReceive(s_results,&result,0));assert(result.result==CP_APPLIED);
 cp_put32(input.bytes+4,124);consume(&input,130);assert(commands==1);assert(!coloplay_usb_result(input.bytes,CP_APPLIED));
 cp_header(input.bytes,CP_KEY,2,123);cp_put16(input.bytes+8,0x4b);cp_put16(input.bytes+10,1);consume(&input,140);assert(key_events==1&&last_key==UI_KEY_PAGE_UP);
 consume(&input,141);assert(key_events==1);cp_put16(input.bytes+2,3);cp_put16(input.bytes+12,1);consume(&input,142);assert(key_events==1&&tx_data[tx_count-1][12]==CP_UNSUPPORTED);
 unsigned before=tx_count;metadata();assert(tx_count>before&&tx_data[before][1]==CP_TRACK);before=tx_count;metadata();assert(tx_count==before);
 ui_navigation_t nav={.view=1,.flags=CP_NAV_FOCUS,.index=2,.count=4};memset(nav.focus,'x',95);nav.focus[95]=0;
 coloplay_usb_publish_navigation(&nav);before=tx_count;navigation(1000);assert(tx_count==before+11&&cp_u16(tx_data[before]+18)==95);
 unsigned used=0;for(unsigned i=before+1;i<tx_count;++i){assert(tx_data[i][1]==CP_NAV_TEXT&&tx_data[i][8]==used);used+=tx_data[i][9];}assert(used==95);
 before=tx_count;navigation(1001);assert(tx_count==before);
 uint8_t bytes[32];cp_usb_encode(bytes,CP_USB_PING,input.bytes);for(unsigned i=0;i<32;++i)received(bytes+i,1,NULL);
 frame_t got;assert(xQueueReceive(s_rx,&got,0)&&got.kind==CP_USB_PING);
 disconnect();assert(!atomic_load(&s_ready)&&!backend_session&&closed==1&&!s_nonce);
 assert(!coloplay_usb_result(input.bytes,CP_APPLIED));
 puts("PASS USB host handshake/challenge, ownership, deduplication, stale session, volume/media replies, navigation fragmentation and disconnect");
 return 0;
}
'''
class ColoPlayUsbTests(unittest.TestCase):
 def test_production_driver(self):
  with tempfile.TemporaryDirectory() as tmp:
   path=Path(tmp);source=(ROOT/'firmware/main/coloplay_usb.c').read_text();source=re.sub(r'^#include .*$', '',source,flags=re.M)
   (path/'driver.c').write_text(PREFIX+source+SUFFIX)
   binary=path/'driver'
   subprocess.run(['cc','-std=c11','-Wno-unused-value','-I'+str(ROOT/'firmware/main'),'-I'+str(ROOT/'app/backend'),str(path/'driver.c'),'-o',str(binary)],check=True)
   subprocess.run([str(binary)],check=True)
if __name__=='__main__':unittest.main()
