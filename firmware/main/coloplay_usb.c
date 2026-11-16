#include "coloplay_usb.h"
#include "coloplay_usb_frame.h"
#include "mpd_client.h"
#include "usb_hid_input.h"
#include "player.h"
#include "volume_grid.h"
#include "usb/cdc_acm_host.h"
#include "usb/usb_host.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>

static _Atomic(QueueHandle_t) s_sleep_mailbox;
static bool s_sleep_supported;
void coloplay_usb_publish_sleep(const cp_sleep_t *state){
    QueueHandle_t mailbox=atomic_load(&s_sleep_mailbox);
    if(!mailbox){mailbox=xQueueCreate(1,sizeof(*state));atomic_store(&s_sleep_mailbox,mailbox);}
    if(mailbox)xQueueOverwrite(mailbox,state);
}
static bool remote_screen_off(void){
    cp_sleep_t state;QueueHandle_t mailbox=atomic_load(&s_sleep_mailbox);
    return s_sleep_supported&&mailbox&&xQueuePeek(mailbox,&state,0)==pdTRUE&&!(state.flags&CP_SLEEP_ON);
}
static const char *TAG="coloplay_usb";
typedef struct {uint8_t kind,bytes[CP_BLE_BYTES];} frame_t;
typedef struct {uint8_t command[CP_BLE_BYTES],result;} result_t;
static QueueHandle_t s_rx,s_results;
static _Atomic(QueueHandle_t) s_nav;
static atomic_uint s_pid,s_session,s_sent,s_received,s_invalid,s_dropped,s_reconnects;
static atomic_bool s_gone,s_open,s_ready;
static cp_usb_parser_t s_parser;
static cdc_acm_dev_hdl_t s_device;
static uint32_t s_nonce,s_challenge;
static int64_t s_last_rx,s_probe_at,s_next_state,s_next_nav;
static uint16_t s_recent[32],s_nav_revision;
static uint8_t s_recent_result[32];static unsigned s_recent_at;
static ui_navigation_t s_navigation;
static bool s_have_nav;
static bool s_star_feedback,s_navigation_heading;
static uint64_t s_track;
static char s_title[64],s_artist[64];

static void new_device(usb_device_handle_t device){
    const usb_device_desc_t *desc=NULL;
    if(usb_host_get_device_descriptor(device,&desc)==ESP_OK&&desc&&desc->idVendor==0x303a&&
       (desc->idProduct==0x1001||desc->idProduct==0x4004))atomic_store(&s_pid,desc->idProduct);
}
static bool received(const uint8_t *bytes,size_t length,void *arg){
    (void)arg;
    for(size_t i=0;i<length;++i){frame_t frame;
        if(cp_usb_feed(&s_parser,bytes[i],&frame.kind,frame.bytes)){
            atomic_fetch_add(&s_received,1);
            if(xQueueSend(s_rx,&frame,0)!=pdTRUE)atomic_fetch_add(&s_dropped,1);
        }
    }
    atomic_store(&s_invalid,s_parser.invalid);return true;
}
static void device_event(const cdc_acm_host_dev_event_data_t *event,void *arg){
    (void)arg;
    if(event->type==CDC_ACM_HOST_DEVICE_DISCONNECTED||event->type==CDC_ACM_HOST_ERROR)atomic_store(&s_gone,true);
}
static bool send_frame(uint8_t kind,const uint8_t *payload){
    uint8_t bytes[CP_USB_FRAME_BYTES];cp_usb_encode(bytes,kind,payload);
    if(!s_device||atomic_load(&s_gone))return false;
    if(cdc_acm_host_data_tx_blocking(s_device,bytes,sizeof(bytes),50)!=ESP_OK){atomic_store(&s_gone,true);return false;}
    atomic_fetch_add(&s_sent,1);return true;
}
static void hello(uint8_t *data){
    cp_header(data,CP_HELLO,0,s_nonce);data[8]=player_volume_steps();
    cp_put_float(data+12,1.0f/volume_grid_count(data[8]));
    data[16]=CP_CAP_VOLUME_FLOAT|CP_CAP_VOLUME_PREDICT|CP_CAP_VOLUME_SCALE|CP_CAP_NAVIGATION;
    if(s_star_feedback)data[17]|=CP_FEATURE_STAR;
    if(s_navigation_heading)data[17]|=CP_FEATURE_NAV_HEADING;
    if(s_sleep_supported)data[17]|=CP_FEATURE_SLEEP;
}
static bool state(uint16_t request,uint8_t result){
    mpd_state_t state={0};mpd_client_get_remote_state(&state);
    uint8_t data[CP_BLE_BYTES];cp_header(data,CP_STATE,request,s_nonce);
    data[8]=(uint8_t)(int8_t)(state.connected?volume_percent(state.volume):-1);
    data[9]=state.connected?state.play_status:255;
    data[10]=cp_state_flags(state.connected,state.track_key,state.starred,mpd_client_coloplay_seek_state(),s_star_feedback);data[11]=result;
    cp_put32(data+12,state.elapsed_ms);cp_put32(data+16,state.duration_ms);
    if(!send_frame(CP_USB_DATA,data))return false;
    cp_header(data,CP_VOLUME_STATE,request,s_nonce);cp_put_float(data+8,state.connected?state.volume:-1.0f);
    data[12]=result;data[13]=player_volume_steps();cp_put16(data+14,state.volume_request);
    cp_put_float(data+16,state.connected&&state.volume_grid_steps==data[13]?state.volume_grid_target:-1.0f);
    if(!send_frame(CP_USB_DATA,data))return false;
    cp_sleep_t sleep;QueueHandle_t mailbox=atomic_load(&s_sleep_mailbox);
    if(s_sleep_supported&&mailbox&&xQueuePeek(mailbox,&sleep,0)==pdTRUE){cp_sleep_packet(data,s_nonce,&sleep);return send_frame(CP_USB_DATA,data);}
    return true;
}
static bool key(uint16_t usage,uint16_t modifiers,ui_key_t *out){
    if((usage==0x55&&!modifiers)||(usage==0x25&&(modifiers==2||modifiers==32))){*out=UI_KEY_STAR;return true;}
    if(modifiers)return false;
    switch(usage){
    case 0x52:*out=UI_KEY_UP;break;case 0x51:*out=UI_KEY_DOWN;break;
    case 0x28:*out=UI_KEY_ENTER;break;case 0x29:*out=UI_KEY_BACK;break;
    case 0x4b:*out=UI_KEY_PAGE_UP;break;case 0x4e:*out=UI_KEY_PAGE_DOWN;break;
    case 0x2f:*out=UI_KEY_SCREEN_PREV;break;case 0x30:*out=UI_KEY_SCREEN_NEXT;break;
    case 0x06:*out=UI_KEY_CONTEXT;break;
    default:if(usage<0x1e||usage>0x24)return false;*out=(ui_key_t)(UI_KEY_1+usage-0x1e);break;
    }return true;
}
static void command(const uint8_t *data){
    if(!cp_command_valid(data,CP_BLE_BYTES)||!mpd_client_coloplay_accepts(s_nonce)){atomic_fetch_add(&s_invalid,1);return;}
    const uint16_t id=cp_u16(data+2);
    for(unsigned i=0;i<32;++i)if(s_recent[i]==id){state(id,s_recent_result[i]);return;}
    uint8_t result=CP_ACCEPTED;
    if(data[1]==CP_SLEEP){
        if(!s_sleep_supported)result=CP_UNSUPPORTED;
        else{usb_hid_input_post_key(cp_u16(data+8)==CP_SLEEP_OFF?UI_KEY_SCREEN_OFF:UI_KEY_ACTIVITY);result=CP_APPLIED;}
    }else if(data[1]!=CP_RELEASE&&remote_screen_off()){
        usb_hid_input_post_key(UI_KEY_ACTIVITY);result=CP_NO_CHANGE;
    }else if(data[1]==CP_KEY){
        ui_key_t value;
        if(!key(cp_u16(data+8),cp_u16(data+12),&value))result=CP_UNSUPPORTED;
        else{for(unsigned n=0;n<cp_u16(data+10);++n)usb_hid_input_post_key(value);result=CP_APPLIED;}
    }else{
        if(s_sleep_supported&&data[1]!=CP_RELEASE)usb_hid_input_post_key(UI_KEY_ACTIVITY);
        const esp_err_t err=mpd_client_coloplay_command(data);
        if(err!=ESP_OK)result=err==ESP_ERR_NO_MEM?CP_BUSY:CP_FAILED;
    }
    s_recent[s_recent_at]=id;s_recent_result[s_recent_at]=result;s_recent_at=(s_recent_at+1)%32;
    state(id,result);
}
static void consume(const frame_t *frame,int64_t now){
    const uint8_t *data=frame->bytes;
    if(frame->kind==CP_USB_OFFER&&cp_packet(data,CP_BLE_BYTES)&&data[1]==CP_HELLO&&cp_u32(data+4)&&cp_u32(data+9)==s_challenge&&
       (data[8]&(CP_CAP_STATE|CP_CAP_VOLUME_FLOAT|CP_CAP_NAVIGATION))==(CP_CAP_STATE|CP_CAP_VOLUME_FLOAT|CP_CAP_NAVIGATION)){
        if(!s_nonce)s_nonce=cp_u32(data+4);
        if(s_nonce!=cp_u32(data+4))return;
        s_sleep_supported=(data[17]&CP_FEATURE_SLEEP)!=0;
        s_star_feedback=(data[17]&CP_FEATURE_STAR)!=0;
        s_navigation_heading=(data[8]&CP_CAP_NAVIGATION)&&(data[17]&CP_FEATURE_NAV_HEADING);
        s_last_rx=now;uint8_t out[CP_BLE_BYTES];hello(out);send_frame(CP_USB_HELLO,out);return;
    }
    if(!s_nonce||!cp_packet(data,CP_BLE_BYTES)||cp_u32(data+4)!=s_nonce){atomic_fetch_add(&s_invalid,1);return;}
    if(frame->kind==CP_USB_READY&&!atomic_load(&s_ready)){
        if(data[1]!=CP_HELLO||data[8]!=player_volume_steps())return;
        mpd_client_coloplay_usb_begin(s_nonce);atomic_store(&s_session,s_nonce);atomic_store(&s_ready,true);
        s_last_rx=now;s_next_state=s_next_nav=0;s_have_nav=false;s_track=UINT64_MAX;
        ESP_LOGI(TAG,"ColoPlay CDC-ACM handshake complete; USB owns input");return;
    }
    if(!atomic_load(&s_ready))return;
    if(frame->kind==CP_USB_PING){s_last_rx=now;return;}
    if(frame->kind==CP_USB_DATA){s_last_rx=now;command(data);}
}
static void metadata(void){
    mpd_state_t remote={0};mpd_client_get_remote_state(&remote);
    if(remote.track_key==s_track&&!strncmp(s_title,remote.title,63)&&!strncmp(s_artist,remote.artist,63))return;
    s_track=remote.track_key;snprintf(s_title,sizeof(s_title),"%.63s",remote.title);snprintf(s_artist,sizeof(s_artist),"%.63s",remote.artist);
    uint8_t data[CP_BLE_BYTES];cp_header(data,CP_TRACK,0,s_nonce);
    cp_put32(data+8,(uint32_t)s_track);cp_put32(data+12,(uint32_t)(s_track>>32));cp_put32(data+16,remote.revision);send_frame(CP_USB_DATA,data);
    for(unsigned field=1;field<=2;++field){
        const char *text=field==1?s_title:s_artist;const unsigned length=strlen(text);
        unsigned offset=0;do{
            unsigned n=length-offset;if(n>11)n=11;
            cp_header(data,CP_TEXT,0,s_nonce);data[2]=field|(offset+n==length?0x80:0);data[3]=offset;data[8]=n;
            memcpy(data+9,text+offset,n);if(!send_frame(CP_USB_DATA,data))return;
        offset+=n;
        }while(offset<length);
    }
}
static void navigation(int64_t now){
    const QueueHandle_t mailbox=atomic_load(&s_nav);ui_navigation_t latest;
    if(!mailbox||xQueuePeek(mailbox,&latest,0)!=pdTRUE)return;
    if(s_have_nav&&!memcmp(&latest,&s_navigation,sizeof(latest))&&now<s_next_nav)return;
    s_navigation=latest;s_have_nav=true;s_next_nav=now+1000000;
    if(!++s_nav_revision)++s_nav_revision;
    uint8_t data[CP_BLE_BYTES];
    cp_navigation_t wire={.view=latest.view,.flags=latest.flags,.index=latest.index,.count=latest.count};
    memcpy(wire.focus,latest.focus,sizeof(wire.focus));memcpy(wire.heading,latest.heading,sizeof(wire.heading));
    cp_navigation_header(data,s_nav_revision,s_nonce,&wire,s_navigation_heading);
    if(!send_frame(CP_USB_DATA,data))return;
    for(unsigned field=0;field<(s_navigation_heading?2u:1u);++field){
        const char *text=field?latest.heading:latest.focus;
        for(unsigned offset=0,length=strlen(text);offset<length;){
            unsigned n=length-offset;if(n>10)n=10;
            cp_header(data,field?CP_NAV_HEADING_TEXT:CP_NAV_TEXT,s_nav_revision,s_nonce);
            data[8]=offset;data[9]=n;memcpy(data+10,text+offset,n);
            if(!send_frame(CP_USB_DATA,data))return;
            offset+=n;
        }
    }
}
static void disconnect(void){
    const bool was_ready=atomic_exchange(&s_ready,false);atomic_store(&s_session,0);
    if(was_ready){mpd_client_coloplay_usb_release();ESP_LOGI(TAG,"USB controller released; BLE may resume");}
    if(s_device){cdc_acm_host_close(s_device);s_device=NULL;}
    atomic_store(&s_open,false);s_nonce=0;s_last_rx=s_probe_at=0;s_star_feedback=s_navigation_heading=s_sleep_supported=false;
    xQueueReset(s_rx);xQueueReset(s_results);memset(s_recent,0,sizeof(s_recent));s_recent_at=0;
    // Driver callbacks have stopped after close; parser has one producer again.
    memset(&s_parser,0,sizeof(s_parser));atomic_store(&s_gone,false);
}
static void usb_task(void *arg){
    (void)arg;
    unsigned scan=0;
    for(;;){
        if(!s_device){
            // Devices attached before driver installation may have no new-device callback.
            const unsigned announced=atomic_load(&s_pid);
            const unsigned pid=announced?announced:((scan++&1)?0x4004:0x1001);
            if(pid){
                const cdc_acm_host_device_config_t cfg={.connection_timeout_ms=100,.out_buffer_size=256,.in_buffer_size=512,.user_arg=NULL,.event_cb=device_event,.data_cb=received};
                if(cdc_acm_host_open(0x303a,pid,0,&cfg,&s_device)==ESP_OK){
                    atomic_store(&s_open,true);atomic_fetch_add(&s_reconnects,1);s_last_rx=esp_timer_get_time();s_challenge=esp_random();if(!s_challenge)s_challenge=1;
                    // Never use 1200 baud or assert RTS: those can reset an ESP.
                    cdc_acm_host_set_control_line_state(s_device,true,false);
                    ESP_LOGI(TAG,"CDC-ACM open 303a:%04x; waiting for ColoPlay handshake",pid);
                }
            }
            if(!s_device){vTaskDelay(pdMS_TO_TICKS(250));continue;}
        }
        const int64_t now=esp_timer_get_time();
        if(atomic_load(&s_gone)||(s_last_rx&&now-s_last_rx>1000000)){disconnect();vTaskDelay(pdMS_TO_TICKS(100));continue;}
        frame_t frame;
        for(unsigned n=0;n<32&&xQueueReceive(s_rx,&frame,0)==pdTRUE;++n)consume(&frame,now);
        if(!atomic_load(&s_ready)){
            if(now>=s_probe_at){uint8_t data[CP_BLE_BYTES];cp_header(data,CP_HELLO,0,s_challenge);send_frame(CP_USB_PROBE,data);s_probe_at=now+250000;}
        }else{
            result_t result;
            for(unsigned n=0;n<12&&xQueueReceive(s_results,&result,0)==pdTRUE;++n){
                if(cp_u32(result.command+4)!=s_nonce)continue;
                const uint16_t id=cp_u16(result.command+2);
                for(unsigned i=0;i<32;++i)if(s_recent[i]==id)s_recent_result[i]=result.result;
                state(id,result.result);
            }
            if(now>=s_next_state){s_next_state=now+250000;uint8_t data[CP_BLE_BYTES];hello(data);send_frame(CP_USB_DATA,data);state(0,0);metadata();}
            navigation(now);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
void coloplay_usb_start(void){
    s_rx=xQueueCreate(64,sizeof(frame_t));s_results=xQueueCreate(32,sizeof(result_t));
    if(!s_rx||!s_results){ESP_LOGE(TAG,"controller queues unavailable");return;}
    const cdc_acm_host_driver_config_t cfg={.driver_task_stack_size=4096,.driver_task_priority=8,.xCoreID=0,.new_dev_cb=new_device};
    const esp_err_t err=cdc_acm_host_install(&cfg);
    if(err!=ESP_OK){ESP_LOGE(TAG,"CDC host unavailable: %s",esp_err_to_name(err));return;}
    if(xTaskCreateWithCaps(usb_task,"coloplay_usb",8192,NULL,5,NULL,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS)ESP_LOGE(TAG,"controller task unavailable");
}
void coloplay_usb_publish_navigation(const ui_navigation_t *state){
    QueueHandle_t mailbox=atomic_load(&s_nav);
    if(!mailbox){mailbox=xQueueCreate(1,sizeof(*state));atomic_store(&s_nav,mailbox);}
    if(mailbox)xQueueOverwrite(mailbox,state);
}
bool coloplay_usb_result(const uint8_t *command,uint8_t result){
    if(!s_results||!atomic_load(&s_ready)||cp_u32(command+4)!=atomic_load(&s_session))return false;
    result_t out;memcpy(out.command,command,CP_BLE_BYTES);out.result=result;
    if(xQueueSend(s_results,&out,0)!=pdTRUE)atomic_fetch_add(&s_dropped,1);
    return true;
}
void coloplay_usb_json(cJSON *root){
    cJSON *out=cJSON_AddObjectToObject(root,"coloplay_usb");
    cJSON_AddBoolToObject(out,"serial_open",atomic_load(&s_open));cJSON_AddBoolToObject(out,"connected",atomic_load(&s_ready));
    cJSON_AddNumberToObject(out,"session",atomic_load(&s_session));cJSON_AddNumberToObject(out,"rx_frames",atomic_load(&s_received));
    cJSON_AddNumberToObject(out,"tx_frames",atomic_load(&s_sent));cJSON_AddNumberToObject(out,"invalid",atomic_load(&s_invalid));
    cJSON_AddNumberToObject(out,"dropped",atomic_load(&s_dropped));cJSON_AddNumberToObject(out,"opens",atomic_load(&s_reconnects));
}
