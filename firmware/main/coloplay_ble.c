#include "coloplay_ble.h"
#include "coloplay_ble_protocol.h"
#include "mpd_client.h"
#include "usb_hid_input.h"
#include "ui.h"
#include "player.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "nimble/nimble_port.h"
#include "os/os_mbuf.h"
#include <string.h>
#include <stdio.h>

static const char *TAG="coloplay_ble";
static const ble_uuid128_t s_service=BLE_UUID128_INIT(0xb5,0x17,0x1d,0x62,0x1d,0x12,0x5d,0x9f,0xcb,0x4a,0x69,0xc1,0x01,0,0x1c,0xa9);
static uint16_t s_start,s_end,s_conn=0xffff,s_caps,s_command,s_command_end,s_state,s_cccd;
static uint32_t s_generation,s_session;
static bool s_ready,s_writing;
static int64_t s_write_deadline,s_next_state;
static uint16_t s_last_id;static uint8_t s_last_result;
static uint16_t s_recent[16];static uint8_t s_recent_result[16];static unsigned s_recent_next;
static struct ble_npl_event s_result_event;
static int s_native_steps;
static void (*s_on_ready)(void);static void (*s_on_failed)(const char*);
typedef struct {uint8_t command[CP_BLE_BYTES],result;} result_t;
static QueueHandle_t s_results;
static uint8_t s_out[32][CP_BLE_BYTES];static unsigned s_read,s_count;
static uint64_t s_track=UINT64_MAX;static unsigned s_text_field,s_text_offset;
static char s_title[64],s_artist[64];

static void fail(const char *reason){s_ready=false;if(s_on_failed)s_on_failed(reason);}
static bool current(uint16_t conn,void *arg){return conn==s_conn&&(uint32_t)(uintptr_t)arg==s_generation;}
static void *generation(void){return (void*)(uintptr_t)s_generation;}
static bool matches(const ble_uuid_t *uuid,unsigned number){
    ble_uuid128_t value=s_service;value.value[12]=number;return ble_uuid_cmp(uuid,&value.u)==0;
}
static void pump(void);
static int wrote(uint16_t conn,const struct ble_gatt_error *error,struct ble_gatt_attr *attr,void *arg){
    (void)attr;if(!current(conn,arg)){return 0;}s_writing=false;
    if(error&&error->status){fail("ColoPlay state write failed");return 0;}pump();return 0;
}
static void pump(void){
    if(!s_ready||s_writing||!s_count)return;
    int rc=ble_gattc_write_flat(s_conn,s_state,s_out[s_read],CP_BLE_BYTES,wrote,generation());
    if(rc){fail("ColoPlay state send failed");return;}
    s_read=(s_read+1)%32;--s_count;s_writing=true;s_write_deadline=esp_timer_get_time()+5000000;
}
static bool queue(const uint8_t *frame){
    if(s_count>=32){return false;}memcpy(s_out[(s_read+s_count)%32],frame,CP_BLE_BYTES);++s_count;pump();return true;
}
static void state_packet(uint8_t *out,uint16_t request,uint8_t result){
    mpd_state_t state={0};mpd_client_get_remote_state(&state);
    cp_header(out,CP_STATE,request,s_session);out[8]=(uint8_t)(int8_t)(state.connected?state.volume:-1);
    out[9]=state.connected?(uint8_t)state.play_status:255;
    out[10]=(state.connected?0x80:0)|mpd_client_coloplay_seek_state();out[11]=result;
    cp_put32(out+12,state.elapsed_ms);cp_put32(out+16,state.duration_ms);
}
void coloplay_ble_reset(void){
    ++s_generation;s_start=s_end=s_caps=s_command=s_command_end=s_state=s_cccd=0;s_conn=0xffff;s_session=0;s_ready=s_writing=false;s_read=s_count=0;s_track=UINT64_MAX;
    memset(s_recent,0,sizeof(s_recent));s_recent_next=0;s_native_steps=0;
    if(s_results){xQueueReset(s_results);}mpd_client_coloplay_release();
}
bool coloplay_ble_connected(void){return s_ready;}
void coloplay_ble_service(const ble_uuid_t *uuid,uint16_t start,uint16_t end){if(matches(uuid,1)){s_start=start;s_end=end;}}
static int subscribed(uint16_t conn,const struct ble_gatt_error *error,struct ble_gatt_attr *attr,void *arg);
void coloplay_ble_tick(int64_t now);
static void result_event(struct ble_npl_event *event){(void)event;coloplay_ble_tick(esp_timer_get_time());}
static int hello_written(uint16_t conn,const struct ble_gatt_error *error,struct ble_gatt_attr *attr,void *arg){
    (void)attr;if(!current(conn,arg)){return 0;}
    if(error&&error->status){fail("ColoPlay handshake rejected");return 0;}
    s_ready=true;s_next_state=0;s_last_id=0;s_last_result=0;mpd_client_coloplay_begin(s_session);if(s_on_ready)s_on_ready();
    struct ble_gap_upd_params params={.itvl_min=6,.itvl_max=12,.latency=0,.supervision_timeout=500};ble_gap_update_params(s_conn,&params);
    ESP_LOGI(TAG,"ColoPlay command/state extension connected");return 0;
}
static int subscribed(uint16_t conn,const struct ble_gatt_error *error,struct ble_gatt_attr *attr,void *arg){
    (void)attr;if(!current(conn,arg)){return 0;}if(error&&error->status){fail("ColoPlay subscription failed");return 0;}
    uint8_t hello[CP_BLE_BYTES];cp_header(hello,CP_HELLO,0,s_session);hello[8]=player_volume_steps();
    if(ble_gattc_write_flat(conn,s_state,hello,sizeof(hello),hello_written,generation())){fail("ColoPlay handshake failed");}return 0;
}
static int descriptor(uint16_t conn,const struct ble_gatt_error *error,uint16_t chr,const struct ble_gatt_dsc *dsc,void *arg){
    (void)chr;if(!current(conn,arg)){return 0;}
    if(error&&error->status==0&&dsc&&dsc->uuid.u.type==BLE_UUID_TYPE_16&&dsc->uuid.u16.value==0x2902)s_cccd=dsc->handle;
    else if(error&&error->status==BLE_HS_EDONE){
        if(!s_cccd){fail("ColoPlay command CCCD missing");return 0;}uint8_t value[2]={1,0};
        if(ble_gattc_write_flat(conn,s_cccd,value,2,subscribed,generation())){fail("ColoPlay subscription failed");}
    }else if(error&&error->status){fail("ColoPlay descriptor discovery failed");}return 0;
}
static int caps_read(uint16_t conn,const struct ble_gatt_error *error,struct ble_gatt_attr *attr,void *arg){
    if(!current(conn,arg)){return 0;}uint8_t bytes[CP_BLE_BYTES];
    if(!error||error->status||!attr||OS_MBUF_PKTLEN(attr->om)!=CP_BLE_BYTES||os_mbuf_copydata(attr->om,0,sizeof(bytes),bytes)||!cp_packet(bytes,sizeof(bytes))||bytes[1]!=CP_HELLO||!cp_u32(bytes+4)){
        fail("ColoPlay protocol capabilities invalid");return 0;
    }
    s_session=cp_u32(bytes+4);
    if(ble_gattc_disc_all_dscs(conn,s_command,s_command_end?s_command_end:s_end,descriptor,generation())){fail("ColoPlay descriptor discovery failed");}return 0;
}
static int characteristic(uint16_t conn,const struct ble_gatt_error *error,const struct ble_gatt_chr *chr,void *arg){
    if(!current(conn,arg)){return 0;}
    if(error&&error->status==0&&chr){
        if(s_command&&!s_command_end&&chr->def_handle>s_command)s_command_end=chr->def_handle-1;
        if(matches(&chr->uuid.u,2))s_caps=chr->val_handle;else if(matches(&chr->uuid.u,3))s_command=chr->val_handle;else if(matches(&chr->uuid.u,4))s_state=chr->val_handle;
    }
    else if(error&&error->status==BLE_HS_EDONE){
        if(!s_caps||!s_command||!s_state){fail("ColoPlay characteristics missing");return 0;}
        if(ble_gattc_read(conn,s_caps,caps_read,generation())){fail("ColoPlay capabilities read failed");}
    }else if(error&&error->status){fail("ColoPlay characteristic discovery failed");}return 0;
}
bool coloplay_ble_discover(uint16_t conn,void (*ready)(void),void (*failed)(const char*)){
    if(!s_start){return false;}
    if(!s_results){s_results=xQueueCreate(24,sizeof(result_t));ble_npl_event_init(&s_result_event,result_event,NULL);}s_on_ready=ready;s_on_failed=failed;s_conn=conn;
    if(!s_results||ble_gattc_disc_all_chrs(conn,s_start,s_end,characteristic,generation())){fail("ColoPlay discovery failed");}return true;
}
void coloplay_ble_result(const uint8_t *command,uint8_t result){
    if(!s_results){return;}result_t out;memcpy(out.command,command,CP_BLE_BYTES);out.result=result;
    if(xQueueSend(s_results,&out,0)!=pdTRUE)ESP_LOGW(TAG,"result queue full");
    else ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&s_result_event);
}
static bool key(uint16_t usage,uint16_t modifiers,ui_key_t *out){
    /* Both keyboard spellings of '*': keypad multiply, or Shift+8. */
    if((usage==0x55&&!modifiers)||
       (usage==0x25&&(modifiers==0x02||modifiers==0x20))){*out=UI_KEY_STAR;return true;}
    if(modifiers)return false;
    switch(usage){case 0x52:*out=UI_KEY_UP;break;case 0x51:*out=UI_KEY_DOWN;break;case 0x28:*out=UI_KEY_ENTER;break;case 0x29:*out=UI_KEY_BACK;break;case 0x4b:*out=UI_KEY_PAGE_UP;break;case 0x4e:*out=UI_KEY_PAGE_DOWN;break;case 0x2f:*out=UI_KEY_SCREEN_PREV;break;case 0x30:*out=UI_KEY_SCREEN_NEXT;break;case 0x06:*out=UI_KEY_CONTEXT;break;
      default:if(usage>=0x1e&&usage<=0x24){*out=(ui_key_t)(UI_KEY_1+usage-0x1e);break;}return false;}return true;
}
bool coloplay_ble_notification(uint16_t conn,uint16_t handle,const uint8_t *data,unsigned len){
    if(conn!=s_conn||handle!=s_command){return false;}if(!s_ready||!cp_command_valid(data,len)||cp_u32(data+4)!=s_session){return true;}
    const uint16_t id=cp_u16(data+2);uint8_t state[CP_BLE_BYTES];
    for(unsigned i=0;i<16;++i)if(s_recent[i]==id){state_packet(state,id,s_recent_result[i]);queue(state);return true;}
    s_last_id=id;s_last_result=CP_ACCEPTED;
    if(data[1]==CP_KEY){
        ui_key_t value;if(!key(cp_u16(data+8),cp_u16(data+12),&value))s_last_result=CP_UNSUPPORTED;
        else {for(int n=0;n<cp_u16(data+10);++n)usb_hid_input_post_key(value);s_last_result=CP_APPLIED;}
    }else{
        const esp_err_t err=mpd_client_coloplay_command(data);
        if(err!=ESP_OK)s_last_result=err==ESP_ERR_NO_MEM?CP_BUSY:CP_FAILED;
    }
    s_recent[s_recent_next]=id;s_recent_result[s_recent_next]=s_last_result;s_recent_next=(s_recent_next+1)%16;
    state_packet(state,id,s_last_result);if(!queue(state)){fail("ColoPlay result queue full");}return true;
}
void coloplay_ble_tick(int64_t now){
    if(!s_ready){return;}if(s_writing&&now>s_write_deadline){fail("ColoPlay state write timeout");return;}
    result_t result;for(int n=0;n<12&&s_count<26&&xQueueReceive(s_results,&result,0)==pdTRUE;++n){
        if(cp_u32(result.command+4)!=s_session){continue;}uint8_t data[CP_BLE_BYTES];const uint16_t id=cp_u16(result.command+2);
        state_packet(data,id,result.result);queue(data);if(id==s_last_id)s_last_result=result.result;
        for(unsigned i=0;i<16;++i)if(s_recent[i]==id)s_recent_result[i]=result.result;
    }
    if(now>=s_next_state&&s_count<8){
        s_next_state=now+400000;uint8_t data[CP_BLE_BYTES];state_packet(data,0,0);queue(data);
        const int steps=player_volume_steps();if(steps!=s_native_steps){s_native_steps=steps;cp_header(data,CP_HELLO,0,s_session);data[8]=steps;queue(data);}
        mpd_state_t state={0};mpd_client_get_remote_state(&state);
        if(state.track_key!=s_track||strncmp(s_title,state.title,63)||strncmp(s_artist,state.artist,63)){
            s_track=state.track_key;cp_header(data,CP_TRACK,0,s_session);cp_put32(data+8,(uint32_t)s_track);cp_put32(data+12,(uint32_t)(s_track>>32));cp_put32(data+16,state.revision);queue(data);
            snprintf(s_title,sizeof(s_title),"%.63s",state.title);snprintf(s_artist,sizeof(s_artist),"%.63s",state.artist);s_text_field=1;s_text_offset=0;
        }
    }
    for(int n=0;n<3&&s_text_field<=2&&s_count<8;++n){
        const char *text=s_text_field==1?s_title:s_artist;unsigned len=strlen(text);if(s_text_offset>len){++s_text_field;s_text_offset=0;continue;}
        unsigned bytes=len-s_text_offset;if(bytes>11)bytes=11;uint8_t data[CP_BLE_BYTES];cp_header(data,CP_TEXT,0,s_session);data[2]=s_text_field|(s_text_offset+bytes>=len?0x80:0);data[3]=s_text_offset;data[8]=bytes;memcpy(data+9,text+s_text_offset,bytes);queue(data);s_text_offset+=bytes;
        if(s_text_offset>=len){++s_text_field;s_text_offset=0;}
    }
    pump();
}
