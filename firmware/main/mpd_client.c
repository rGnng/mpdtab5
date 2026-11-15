/* MPD transport for the shared Tab5 player/UI. The worker owns every socket,
 * URI and outgoing command. UI and HID callbacks only enqueue bounded work. */
#include "mpd_client.h"
#include "mpd_wire.h"
#include "player.h"
#include "text_util.h"
#include "volume_grid.h"
#include "coloplay_ble.h"
#include "coloplay_ble_protocol.h"
#include "app_config.h"
#include "jpeg_thumbnail.h"
#include "thumb_cache.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define SERVER_FILE "/sdcard/mpd/server.json"
#define TRACK_LIMIT 24000
#define QUEUE_LIMIT 24000
#define URI_BYTES (3*1024*1024)
#define URI_SLOTS 65536
#define ART_BYTES (360*360*2)
static const char *TAG="mpd";
static SemaphoreHandle_t mutex;
static QueueHandle_t commands;
static mpd_config_t config={.port=6600};
static uint32_t config_revision=1;
static bool rescan;
static mpd_state_t state={.battery=-1,.volume=-1,.queue_index=-1};
static char status_text[192]="Set MPD server in the web interface";
static uint32_t art_revision=1, queue_revision=1;
static uint8_t *art;
static bool art_valid;
static uint64_t *queue_keys;
static uint32_t *queue_ids;
static int queue_size;
static uint8_t hold_bit;
static TaskHandle_t worker;

typedef struct { uint64_t id; uint32_t offset; } uri_entry_t;
static uri_entry_t *uris;
static uint16_t *uri_slots;
static FILE *uri_store;
static char uri_read_buffer[2048];
static unsigned uri_count, uri_used;
static mpd_wire_t wire, events;
static bool unpaged_server;
static unsigned database_songs;
static unsigned event_flags;
static int last_playlist=-1;
static bool queue_refresh_pending;
static uint64_t *star_keys;
static int star_count;
static uint32_t star_revision;
static bool stars_refresh_pending;
static char database_revision[64];
static int mpd_playlist_version;
static bool mpd_updating;
static unsigned reported_queue_size;
static bool remote_repeat, remote_single;
static float muted_volume=0.5f;
static volume_grid_cursor_t volume_cursor;
static uint16_t coloplay_volume_completed;
static float server_volume=-1.0f;
static bool volume_integer_only=false;
static uint8_t coloplay_seek;
static uint32_t coloplay_session;
static int64_t coloplay_seek_deadline,coloplay_next_seek;
static char current_uri[2048];
static char library_id[64];
static void artwork_register(uint64_t album, uint64_t track);
static void artwork_reset(void);
static void artwork_step(uint32_t generation);

typedef enum { CMD_TEXT, CMD_PLAY, CMD_ALL, CMD_JUMP, CMD_STAR, CMD_CLEAR_STARS, CMD_COLOPLAY } command_kind_t;
typedef struct {
    command_kind_t kind;
    uint32_t generation;
    int count, start;
    bool shuffle;
    uint32_t queue_id;
    uint64_t *keys;
    char text[80];
    uint8_t coloplay[CP_BLE_BYTES];
    uint8_t remote_result;
} command_t;
static void take(void) { xSemaphoreTake(mutex,portMAX_DELAY); }
static void give(void) { xSemaphoreGive(mutex); }
static void message(const char *s) { take(); snprintf(status_text,sizeof(status_text),"%s",s); give(); }
static void publish(const mpd_state_t *s) {
    take();
    /* Elapsed time is sampled separately by the UI. A successful poll alone
     * must not invalidate playback chrome on every library screen. */
#define CHANGED(field) (state.field != s->field)
    bool changed = CHANGED(connected) || CHANGED(identified) || CHANGED(library_syncing) ||
        CHANGED(play_status) || CHANGED(track_key) || CHANGED(album_pid) ||
        CHANGED(duration_ms) || CHANGED(queue_index) || CHANGED(queue_count) ||
        CHANGED(shuffle) || CHANGED(starred) || CHANGED(repeat) || CHANGED(volume) ||
        CHANGED(battery) || CHANGED(charging) || CHANGED(max_current_ma) ||
        CHANGED(device_will_charge) || CHANGED(accessory_power_mode) ||
        CHANGED(external_charger) || CHANGED(charging_state) ||
        strcmp(state.title, s->title) || strcmp(state.artist, s->artist) ||
        strcmp(state.album, s->album) || strcmp(state.device_name, s->device_name);
#undef CHANGED
    uint32_t revision = state.revision + changed;
    state = *s;
    state.volume_request=coloplay_volume_completed;
    state.revision = revision;
    give();
}
static void identity(const mpd_config_t *c, char *out, size_t cap) {
    char address[144]; snprintf(address,sizeof(address),"%s:%u",c->host,c->port);
    snprintf(out,cap,"MPD-%08lX",(unsigned long)(uint32_t)mpd_id(address));
}
void mpd_client_config(mpd_config_t *out) { take(); *out=config; give(); }
void mpd_client_library_id(char *out,size_t cap) { take(); identity(&config,out,cap); give(); }
static bool clean_string(const char *s,size_t cap) {
    size_t n=strnlen(s,cap); if(n==cap) return false;
    for(size_t i=0;i<n;i++) if((unsigned char)s[i]<32 || (unsigned char)s[i]==127) return false;
    return true;
}
esp_err_t mpd_client_configure(const mpd_config_t *c) {
    if(!c || !clean_string(c->host,sizeof(c->host)) || !clean_string(c->password,sizeof(c->password)) || !c->port)
        return ESP_ERR_INVALID_ARG;
    if(!app_config_sd_ready()) return ESP_ERR_INVALID_STATE;
    cJSON *json=cJSON_CreateObject(); if(!json) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(json,"host",c->host); cJSON_AddNumberToObject(json,"port",c->port);
    cJSON_AddStringToObject(json,"password",c->password);
    char *text=cJSON_PrintUnformatted(json); cJSON_Delete(json);
    if(!text) return ESP_ERR_NO_MEM;
    /* Serialize writers and publish only a durably written configuration. */
    take();
    FILE *f=fopen(SERVER_FILE ".tmp","wb");
    bool ok=false;
    if(f) { ok=fwrite(text,1,strlen(text),f)==strlen(text); ok=fflush(f)==0 && ok; ok=fclose(f)==0 && ok; }
    free(text);
    if(ok) {
        remove(SERVER_FILE ".bak");
        rename(SERVER_FILE,SERVER_FILE ".bak");
        ok=rename(SERVER_FILE ".tmp",SERVER_FILE)==0;
        if(!ok) rename(SERVER_FILE ".bak",SERVER_FILE);
    }
    if(ok) { config=*c; config_revision++; state.connected=state.identified=false; state.revision++; }
    give();
    return ok ? ESP_OK : ESP_FAIL;
}
void mpd_client_init(void) {
    mutex=xSemaphoreCreateMutex();
    commands=xQueueCreate(16,sizeof(command_t));
    FILE *f=fopen(SERVER_FILE,"rb");
    if(!f) f=fopen(SERVER_FILE ".bak","rb");
    if(f) {
        char data[1024]; size_t n=fread(data,1,sizeof(data)-1,f); fclose(f); data[n]=0;
        cJSON *j=cJSON_Parse(data);
        const cJSON *host=cJSON_GetObjectItemCaseSensitive(j,"host"), *port=cJSON_GetObjectItemCaseSensitive(j,"port"), *password=cJSON_GetObjectItemCaseSensitive(j,"password");
        if(cJSON_IsString(host) && strlen(host->valuestring)<sizeof(config.host) && clean_string(host->valuestring,sizeof(config.host))) strcpy(config.host,host->valuestring);
        if(cJSON_IsNumber(port) && port->valuedouble==port->valueint && port->valueint>0 && port->valueint<=65535) config.port=port->valueint;
        if(cJSON_IsString(password) && strlen(password->valuestring)<sizeof(config.password) && clean_string(password->valuestring,sizeof(config.password))) strcpy(config.password,password->valuestring);
        cJSON_Delete(j);
    }
    identity(&config,library_id,sizeof(library_id));
    app_config_set_library_id(library_id);
    mpd_wire_init(&wire);
    mpd_wire_init(&events);
}
void mpd_client_rescan(void) { take(); rescan=true; give(); }
const char *mpd_client_status(void) {
    /* Each caller receives its own task-local copy? ESP tasks don't have C TLS;
     * return stable storage, writers only replace this bounded display string. */
    return status_text;
}
bool mpd_client_get_state(mpd_state_t *out) { if(!mutex || !out) return false; take(); *out=state; give(); return true; }
bool mpd_client_get_remote_state(mpd_state_t *out){if(!mutex||!out)return false;take();*out=state;give();return true;}
void mpd_client_coloplay_begin(uint32_t session){if(!mutex)return;take();coloplay_session=session;coloplay_seek=0;coloplay_volume_completed=state.volume_request=0;give();}
void mpd_client_coloplay_release(void){if(!mutex)return;take();coloplay_session=0;coloplay_seek=0;coloplay_volume_completed=state.volume_request=0;give();}
uint8_t mpd_client_coloplay_seek_state(void){if(!mutex)return 0;take();uint8_t value=esp_timer_get_time()<coloplay_seek_deadline?coloplay_seek:0;give();return value;}
bool mpd_client_is_enabled(void) { return true; }
bool mpd_client_is_connected(void) { mpd_state_t s={0}; return mpd_client_get_state(&s) && s.connected; }
bool mpd_client_is_identified(void) { return mpd_client_is_connected(); }
bool mpd_client_is_playing(void) { mpd_state_t s={0}; return mpd_client_get_state(&s) && s.play_status==1; }
static esp_err_t enqueue(command_t *c) {
    take(); c->generation=config_revision; bool online=state.connected; give();
    if(!online) { free(c->keys); return ESP_ERR_INVALID_STATE; }
    if(xQueueSend(commands,c,0)!=pdPASS) { free(c->keys); message("MPD command queue full"); return ESP_ERR_NO_MEM; }
    return ESP_OK;
}
esp_err_t mpd_client_coloplay_command(const uint8_t *packet){
    if(!cp_command_valid(packet,CP_BLE_BYTES))return ESP_ERR_INVALID_ARG;
    command_t command={.kind=CMD_COLOPLAY,.remote_result=CP_APPLIED};memcpy(command.coloplay,packet,CP_BLE_BYTES);return enqueue(&command);
}
static esp_err_t text_command(const char *text) {
    command_t c={.kind=CMD_TEXT}; snprintf(c.text,sizeof(c.text),"%s",text); return enqueue(&c);
}
esp_err_t mpd_client_seek_ms(uint32_t ms) { char cmd[64]; snprintf(cmd,sizeof(cmd),"seekcur %.3f",ms/1000.0); return text_command(cmd); }
esp_err_t mpd_client_set_shuffle(bool on) { return text_command(on ? "random 1" : "random 0"); }
esp_err_t mpd_client_toggle_star(uint64_t key) {
    if(!key) return ESP_ERR_INVALID_ARG;
    command_t c={.kind=CMD_STAR,.count=1};
    c.keys=malloc(sizeof(*c.keys)); if(!c.keys) return ESP_ERR_NO_MEM;
    c.keys[0]=key; return enqueue(&c);
}
esp_err_t mpd_client_clear_stars(void) { command_t c={.kind=CMD_CLEAR_STARS}; return enqueue(&c); }
int mpd_client_star_count(void) { take(); int n=star_count; give(); return n; }
uint32_t mpd_client_star_revision(void) { take(); uint32_t r=star_revision; give(); return r; }
bool mpd_client_star_get(int index,uint64_t *key) {
    take(); bool ok=key && index>=0 && index<star_count;
    if(ok) *key=star_keys[index];
    give(); return ok;
}
int mpd_client_star_copy(uint64_t *keys,int capacity) {
    if(!keys || capacity<=0) return 0;
    take(); int n=star_count<capacity ? star_count : capacity;
    if(n) memcpy(keys,star_keys,n*sizeof(*keys));
    give(); return n;
}
static int star_position(uint64_t key) {
    for(int i=0;i<star_count;i++) if(star_keys[i]==key) return i;
    return -1;
}
esp_err_t mpd_client_queue_jump(int index) {
    command_t c={.kind=CMD_JUMP};
    take();
    if(index>=0 && index<queue_size) c.queue_id=queue_ids[index];
    give();
    return c.queue_id ? enqueue(&c) : ESP_ERR_INVALID_ARG;
}
esp_err_t mpd_client_hid_command(const char *name) {
    if(!strcmp(name,"play")) return text_command("play");
    if(!strcmp(name,"pause")) return text_command("pause 1");
    if(!strcmp(name,"next") || !strcmp(name,"previous") || !strcmp(name,"stop")) return text_command(name);
    if(!strcmp(name,"volume_up")) return text_command("@volume_up");
    if(!strcmp(name,"volume_down")) return text_command("@volume_down");
    /* Evaluate toggles on the worker using its latest authoritative state. */
    if(!strcmp(name,"shuffle")) return text_command("@shuffle");
    if(!strcmp(name,"repeat")) return text_command("@repeat");
    if(!strcmp(name,"mute")) return text_command("@mute");
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t mpd_client_hid_raw_bit(uint8_t bit) {
    static const char *names[]={"play","pause","next","previous","volume_up","volume_down","mute","stop","shuffle","repeat"};
    return bit<10 ? mpd_client_hid_command(names[bit]) : ESP_ERR_NOT_SUPPORTED;
}
esp_err_t mpd_client_hid_press_bit(uint8_t bit) { take(); hold_bit=bit; give(); return ESP_OK; }
esp_err_t mpd_client_hid_release(void) { take(); hold_bit=0; give(); return ESP_OK; }
esp_err_t mpd_client_refresh_now_playing(void) { mpd_client_rescan(); return ESP_OK; }
esp_err_t mpd_client_play_track_keys(const uint64_t *keys,int count,int start,bool shuffle) {
    if(!keys || count<=0 || count>TRACK_LIMIT || start<0 || start>=count) return ESP_ERR_INVALID_ARG;
    command_t c={.kind=CMD_PLAY,.count=count,.start=start,.shuffle=shuffle};
    c.keys=heap_caps_malloc(count*sizeof(*keys),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!c.keys) return ESP_ERR_NO_MEM;
    memcpy(c.keys,keys,count*sizeof(*keys)); return enqueue(&c);
}
esp_err_t mpd_client_play_all_songs(uint64_t key) {
    command_t c={.kind=CMD_ALL,.count=1};
    c.keys=malloc(sizeof(key)); if(!c.keys) return ESP_ERR_NO_MEM;
    *c.keys=key; return enqueue(&c);
}
esp_err_t mpd_client_play_current_key(void) { return text_command("play"); }
int mpd_client_queue_list_count(void) { take(); int n=queue_size; give(); return n; }
int mpd_client_queue_list_base(void) { return 0; }
uint32_t mpd_client_queue_list_revision(void) { take(); uint32_t n=queue_revision; give(); return n; }
bool mpd_client_queue_list_get(int i,uint64_t *key) {
    take(); bool ok=i>=0 && i<queue_size; if(ok) *key=queue_keys[i]; give(); return ok;
}
uint32_t mpd_client_artwork_revision(void) { take(); uint32_t n=art_revision; give(); return n; }
bool mpd_client_artwork_copy(uint8_t *dst,size_t len,artwork_quality_t min,artwork_quality_t *actual) {
    take(); bool ok=art_valid && len>=ART_BYTES;
    artwork_quality_t q=ok ? (artwork_quality_t){.width=360,.height=360} : (artwork_quality_t){0};
    if(actual) *actual=q;
    if(ok && (min.width>360 || min.height>360)) ok=false;
    if(ok) memcpy(dst,art,ART_BYTES);
    give(); return ok;
}

static const char *uri_lookup(uint64_t id) {
    unsigned h=(unsigned)id&(URI_SLOTS-1);
    for(unsigned n=0;n<URI_SLOTS;n++,h=(h+1)&(URI_SLOTS-1)) {
        unsigned slot=uri_slots[h]; if(!slot) return NULL;
        if(uris[slot-1].id==id) {
#ifdef ESP_PLATFORM
            if(!uri_store || fseek(uri_store,uris[slot-1].offset,SEEK_SET)!=0) return NULL;
            size_t n=fread(uri_read_buffer,1,sizeof(uri_read_buffer)-1,uri_store);
            uri_read_buffer[n]=0;
            return n ? uri_read_buffer : NULL;
#else
            return uri_pool+uris[slot-1].offset;
#endif
        }
    }
    return NULL;
}
static bool uri_insert(const char *uri) {
    uint64_t id=mpd_id(uri);
    const char *found=uri_lookup(id);
    if(found) return !strcmp(found,uri);
    size_t n=strlen(uri)+1;
    if(uri_count==TRACK_LIMIT) return false;
#ifndef ESP_PLATFORM
    if(uri_used+n>URI_BYTES) return false;
#endif
    unsigned h=(unsigned)id&(URI_SLOTS-1); while(uri_slots[h]) h=(h+1)&(URI_SLOTS-1);
    uris[uri_count]=(uri_entry_t){.id=id,.offset=uri_used};
#ifdef ESP_PLATFORM
    if(!uri_store) return false;
    if(ftell(uri_store)!=(long)uri_used && fseek(uri_store,uri_used,SEEK_SET)!=0) return false;
    if(fwrite(uri,1,n,uri_store)!=n) return false;
#else
    memcpy(uri_pool+uri_used,uri,n);
#endif
    uri_used+=n; uri_slots[h]=++uri_count; return true;
}
typedef struct {
    player_media_item_t item;
    char uri[2048];
    bool library, queue, stored, catalog_changed, stars, failed;
    uint32_t modified, song_id;
    uint64_t first_key;
    int playlist;
    int count;
    int capacity;
    uint64_t *keys;
    uint32_t *ids;
} song_parser_t;
static void song_reset(song_parser_t *p) { memset(&p->item,0,sizeof(p->item)); p->uri[0]=0; p->modified=p->song_id=0; }
static bool song_finish(song_parser_t *p) {
    if(!p->uri[0]) return true;
    if(!p->item.title[0]) {
        const char *base=strrchr(p->uri,'/'); text_copy(p->item.title,sizeof(p->item.title),base ? base+1 : p->uri);
    }
    if(!p->item.album[0]) snprintf(p->item.album,sizeof(p->item.album),"%s","Singles / Streams");
    if(!p->item.artist[0]) snprintf(p->item.artist,sizeof(p->item.artist),"%s","Unknown artist");
    if(!p->item.album_artist[0]) snprintf(p->item.album_artist,sizeof(p->item.album_artist),"%s",p->item.artist);
    char album[256]; snprintf(album,sizeof(album),"%s\037%s",p->item.album_artist,p->item.album);
    p->item.pid=mpd_id(p->uri); p->item.album_pid=mpd_id(album);
    if(!p->item.date_added) p->item.date_added=p->modified;
    if(!p->count) p->first_key=p->item.pid;
    p->item.fields=PLAYER_MEDIA_MPD|(1u<<1)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<9)|(1u<<12)|(1u<<14);
    if(p->library) {
        if(!uri_insert(p->uri)) return false;
#ifdef ESP_PLATFORM
        artwork_register(p->item.album_pid,p->item.pid);
#endif
        player_library_update(&p->item);
        p->catalog_changed=true;
    }
    if(p->stars && !p->failed) {
        if(p->count>=TRACK_LIMIT) p->failed=true;
        else if(p->count>=p->capacity) {
            int capacity=p->capacity ? p->capacity*2 : 32;
            if(capacity>TRACK_LIMIT) capacity=TRACK_LIMIT;
            uint64_t *keys=heap_caps_realloc(p->keys,capacity*sizeof(*keys),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if(keys) { p->keys=keys; p->capacity=capacity; }
            else p->failed=true; /* Drain the response; retain the last confirmed snapshot. */
        }
        if(!p->failed) p->keys[p->count]=p->item.pid;
    }
    if(p->stored && !uri_lookup(p->item.pid)) {
        if(!uri_insert(p->uri)) return false;
#ifdef ESP_PLATFORM
        artwork_register(p->item.album_pid,p->item.pid);
#endif
        player_library_update(&p->item);
        p->catalog_changed=true;
    }
    if(p->queue) {
        /* A queue can grow between status and playlistinfo. Drain the reply
         * and retry with its new size, without losing the control socket. */
        if(p->count >= (p->capacity ? p->capacity : QUEUE_LIMIT)) {
            p->count++;
            return true;
        }
        p->keys[p->count]=p->item.pid;
        if(p->ids) p->ids[p->count]=p->song_id;
        /* Queue-only streams have real metadata and remain selectable. */
        if(!uri_lookup(p->item.pid)) {
            if(!uri_insert(p->uri)) return false;
#ifdef ESP_PLATFORM
            artwork_register(p->item.album_pid,p->item.pid);
#endif
            player_library_update(&p->item);
            p->catalog_changed=true;
        }
    }
    if(p->stored && !p->stars && p->count<1024) player_library_playlist_item(p->playlist,NULL,p->item.pid);
    p->count++; return true;
}
static bool song_field(const char *key,const char *value,void *ctx) {
    song_parser_t *p=ctx;
    if(!strcmp(key,"file")) {
        if(!song_finish(p)) return false;
        song_reset(p);
        if(strlen(value)>=sizeof(p->uri)) return false;
        strcpy(p->uri,value);
    }
    else if(!strcmp(key,"added")) p->item.date_added=mpd_timestamp(value);
    else if(!strcmp(key,"Last-Modified")) p->modified=mpd_timestamp(value);
    else if(!strcmp(key,"Id")) p->song_id=(uint32_t)strtoul(value,NULL,10);
    else if(!strcasecmp(key,"Title")) text_copy(p->item.title,sizeof(p->item.title),value);
    else if(!strcasecmp(key,"Artist")) text_copy(p->item.artist,sizeof(p->item.artist),value);
    else if(!strcasecmp(key,"AlbumArtist")) text_copy(p->item.album_artist,sizeof(p->item.album_artist),value);
    else if(!strcasecmp(key,"Album")) text_copy(p->item.album,sizeof(p->item.album),value);
    else if(!strcmp(key,"duration")) p->item.duration_ms=(uint32_t)(strtod(value,NULL)*1000);
    else if(!strcmp(key,"Time") && !p->item.duration_ms) p->item.duration_ms=(uint32_t)strtoul(value,NULL,10)*1000;
    else if(!strcasecmp(key,"Track")) p->item.track_no=atoi(value);
    else if(!strcasecmp(key,"Disc")) p->item.disc_no=atoi(value);
    else if(!strcasecmp(key,"Date")) p->item.year=atoi(value);
    return true;
}
static bool status_field(const char *key,const char *value,void *ctx) {
    mpd_state_t *s=ctx;
    if(!strcmp(key,"state")) s->play_status=!strcmp(value,"play") ? 1 : !strcmp(value,"pause") ? 2 : 0;
    else if(!strcmp(key,"volume")) {
        char *end;const float percent=strtof(value,&end);
        while(*end==' '||*end=='\t')++end;
        server_volume=end==value||*end||!(percent>=0&&percent<=100)?-1.0f:percent;
        s->volume=server_volume<0 ? -1.0f : server_volume/100.0f;
    }
    else if(!strcmp(key,"random")) s->shuffle=atoi(value)!=0;
    else if(!strcmp(key,"repeat")) remote_repeat=atoi(value)!=0;
    else if(!strcmp(key,"single")) remote_single=atoi(value)!=0;
    else if(!strcmp(key,"elapsed")) s->elapsed_ms=(uint32_t)(strtod(value,NULL)*1000);
    else if(!strcmp(key,"duration")) s->duration_ms=(uint32_t)(strtod(value,NULL)*1000);
    else if(!strcmp(key,"song")) s->queue_index=atoi(value);
    else if(!strcmp(key,"playlist")) mpd_playlist_version=atoi(value);
    else if(!strcmp(key,"playlistlength")) { reported_queue_size=strtoul(value,NULL,10); s->queue_count=reported_queue_size; }
    else if(!strcmp(key,"updating_db")) mpd_updating=true;
    return true;
}
static bool stat_field(const char *key,const char *value,void *ctx) {
    if(!strcmp(key,"db_update")) snprintf(ctx,64,"%s",value);
    else if(!strcmp(key,"songs")) database_songs=(unsigned)strtoul(value,NULL,10);
    return true;
}
static bool simple(const char *cmd) { return mpd_wire_command(&wire,cmd,NULL,NULL,NULL); }
static bool sync_library(void) {
    message("Syncing MPD library...");
    char db[64]={0};
    if(!mpd_wire_command(&wire,"stats",stat_field,NULL,db)) return false;
    snprintf(database_revision,sizeof(database_revision),"%s",db);
#ifdef ESP_PLATFORM
    if(uri_store) fclose(uri_store);
    uri_store=fopen("/sdcard/mpd/uris.bin","w+b");
    if(!uri_store) { message("Cannot create MPD URI cache on SD"); return false; }
#endif
    take(); state.library_syncing=true; state.revision++; give();
#ifdef ESP_PLATFORM
    artwork_reset();
#endif
    player_library_reset();
    memset(uri_slots,0,URI_SLOTS*sizeof(*uri_slots)); uri_count=uri_used=0;
    song_parser_t *p=calloc(1,sizeof(*p));
    bool ok=p!=NULL;
    if(p) p->library=true;
    uint64_t first=0;
    for(unsigned offset=0;ok;offset+=256) {
        char cmd[96];
        song_reset(p); p->count=0;
        snprintf(cmd,sizeof(cmd),"find \"(base '')\" window %u:%u",offset,offset+256);
        ok=mpd_wire_command(&wire,unpaged_server ? "listallinfo" : cmd,song_field,NULL,p) && song_finish(p);
        /* Tracks' filter/window implementation is incomplete. Its empty
         * find response must not hide a nonempty database. Stream its supported
         * listallinfo response once, with the same capacity checks. */
        if(!unpaged_server && !offset && wire.fd>=0 && (!ok || (!p->count && database_songs))) {
            unpaged_server=true; song_reset(p); p->count=0;
            ok=mpd_wire_command(&wire,"listallinfo",song_field,NULL,p) && song_finish(p);
        }
        if(!ok || unpaged_server || p->count<256 || p->count>256 ||
           (database_songs && uri_count>=database_songs)) break;
        if(offset && p->first_key==first) { unpaged_server=true; break; }
        first=p->first_key;
        vTaskDelay(1);
    }
    free(p);
    if(ok) { player_library_complete(); message("MPD connected"); }
    else message(wire.error[0] ? wire.error : "MPD library exceeds controller capacity");
    take(); state.library_syncing=false; state.revision++; give();
    return ok;
}
static bool poll_state(void) {
    mpd_state_t next={.battery=-1,.max_current_ma=-1,.volume=-1,.queue_index=-1,.connected=true,.identified=true};
    take(); snprintf(next.device_name,sizeof(next.device_name),"MPD · %.48s",config.host); give();
    mpd_updating=false; remote_repeat=remote_single=false; reported_queue_size=0; server_volume=-1;
    if(!mpd_wire_command(&wire,"status",status_field,NULL,&next)) return false;
    volume_grid_observe(&volume_cursor,next.volume);
    next.volume_grid_steps=player_volume_steps();
    next.volume_grid_target=volume_cursor.active&&!volume_cursor.pending&&
        volume_cursor.steps==next.volume_grid_steps?volume_cursor.target:-1.0f;
    next.repeat=remote_repeat ? (remote_single ? 1 : 2) : 0;
    song_parser_t parser={0};
    song_parser_t *p=&parser;
    bool ok=mpd_wire_command(&wire,"currentsong",song_field,NULL,p) && song_finish(p);
    if(ok && p->uri[0]) {
        next.track_key=p->item.pid; next.album_pid=p->item.album_pid;
        text_copy(next.title,sizeof(next.title),p->item.title);
        text_copy(next.artist,sizeof(next.artist),p->item.artist);
        text_copy(next.album,sizeof(next.album),p->item.album);
        if(!next.duration_ms) next.duration_ms=p->item.duration_ms;
    }
    if(ok) text_copy(current_uri,sizeof(current_uri),p->uri);
    if(ok) { take(); next.starred=star_position(next.track_key)>=0; give(); publish(&next); }
    return ok;
}
static bool sync_queue(void) {
    song_parser_t parser={0};
    song_parser_t *p=&parser;
    p->queue=true;
    p->capacity=reported_queue_size < 32 ? 32 : reported_queue_size;
    if(p->capacity > QUEUE_LIMIT) p->capacity=QUEUE_LIMIT;
    queue_refresh_pending=true;
    p->ids=heap_caps_malloc(p->capacity*sizeof(uint32_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    p->keys=heap_caps_malloc(p->capacity*sizeof(uint64_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!p->keys || !p->ids) {
        free(p->ids); free(p->keys);
        message("Queue refresh deferred: not enough memory");
        ESP_LOGW(TAG,"queue allocation failed: entries=%d, PSRAM free=%u largest=%u",
            p->capacity, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        return true; /* A local allocation failure is not a lost connection. */
    }
    bool ok=mpd_wire_command(&wire,"playlistinfo",song_field,NULL,p) && song_finish(p);
    /* Playback/queue changes do not invalidate the library or its thumbnails.
     * Only newly discovered queue-only tracks need a catalog publication. */
    if(ok && p->catalog_changed) player_library_complete();
    if(ok && p->count <= p->capacity) {
        take(); memcpy(queue_keys,p->keys,p->count*sizeof(uint64_t)); memcpy(queue_ids,p->ids,p->count*sizeof(uint32_t)); queue_size=p->count; queue_revision++; give();
        queue_refresh_pending=false;
    }
    free(p->ids); free(p->keys);
    return ok || wire.fd>=0;
}

static bool sync_stars(void) {
    song_parser_t parser={.stored=true,.stars=true};
    stars_refresh_pending=true;
    bool ok=mpd_wire_command(&wire,"listplaylistinfo \"Stared\"",song_field,NULL,&parser) && song_finish(&parser);
    bool absent=!ok && wire.fd>=0 && strstr(wire.error,"ACK [50@");
    if(ok && parser.catalog_changed) player_library_complete();
    if((ok && !parser.failed) || absent) {
        if(absent) { free(parser.keys); parser.keys=NULL; parser.count=0; }
        take();
        bool changed=star_count!=parser.count ||
            (star_count && memcmp(star_keys,parser.keys,star_count*sizeof(*star_keys)));
        uint64_t *old=star_keys; star_keys=parser.keys; star_count=parser.count;
        if(changed) star_revision++;
        bool starred=star_position(state.track_key)>=0;
        if(state.starred!=starred) { state.starred=starred; state.revision++; }
        give(); free(old);
        stars_refresh_pending=false;
    } else {
        free(parser.keys);
        message(parser.failed ? "Starred playlist refresh deferred: not enough memory or too many entries" : wire.error);
    }
    return ok || wire.fd>=0;
}
static bool change_star(uint64_t key,bool clear) {
    /* Read immediately before a positional delete so external edits are seen.
     * The requested track key is captured at input time, independent of next. */
    if(!sync_stars()) return false;
    if(stars_refresh_pending) return true;
    char cmd[4160],quoted[4100];
    if(clear) snprintf(cmd,sizeof(cmd),"playlistclear \"Stared\"");
    else {
        int position=star_position(key);
        if(position>=0) snprintf(cmd,sizeof(cmd),"playlistdelete \"Stared\" %d",position);
        else {
            const char *uri=uri_lookup(key);
            if(!uri || !mpd_quote(quoted,sizeof(quoted),uri)) {
                message("Cannot star a track missing from the MPD catalog"); return true;
            }
            snprintf(cmd,sizeof(cmd),"playlistadd \"Stared\" %s",quoted);
        }
    }
    if(!simple(cmd)) return false;
    return sync_stars();
}

typedef struct { char names[32][256]; int count; } playlists_t;
static bool playlist_field(const char *key,const char *value,void *ctx) {
    playlists_t *p=ctx;
    if(!strcmp(key,"playlist") && strcasecmp(value,"Stared") && p->count<32) {
        if(strlen(value)>=sizeof(p->names[0])) return false;
        strcpy(p->names[p->count++],value);
    }
    return true;
}
static bool sync_playlists(void) {
    playlists_t *lists=calloc(1,sizeof(*lists)); if(!lists) return false;
    bool ok=mpd_wire_command(&wire,"listplaylists",playlist_field,NULL,lists), catalog_changed=false;
    if(ok) player_library_playlists_reset();
    for(int i=0;ok && i<lists->count;i++) {
        char quoted[520],cmd[560];
        if(!mpd_quote(quoted,sizeof(quoted),lists->names[i])) { ok=false; break; }
        snprintf(cmd,sizeof(cmd),"listplaylistinfo %s",quoted);
        player_library_playlist_item(i,lists->names[i],0);
        song_parser_t *p=calloc(1,sizeof(*p));
        if(!p) { ok=false; break; }
        p->stored=true; p->playlist=i;
        ok=mpd_wire_command(&wire,cmd,song_field,NULL,p) && song_finish(p);
        catalog_changed|=p->catalog_changed;
        free(p);
    }
    free(lists);
    if(ok && catalog_changed) player_library_complete();
    return ok && sync_stars();
}
typedef struct { FILE *file; unsigned total, received, chunk; } art_transfer_t;
static bool art_field(const char *key,const char *value,void *ctx) {
    art_transfer_t *t=ctx;
    if(!strcmp(key,"size")) {
        unsigned long n=strtoul(value,NULL,10);
        if(n>2*1024*1024 || !n || (t->total && t->total!=n)) return false;
        t->total=n;
    }
    return true;
}
static bool art_binary(const void *data,size_t len,void *ctx) {
    art_transfer_t *t=ctx;
    if(!t->total || t->received+len>t->total) return false;
    if(fwrite(data,1,len,t->file)!=len) return false;
    t->received+=len; t->chunk+=len; return true;
}
#include "mpd_artwork.inc"
static bool play_selection(const uint64_t *keys,int count,int start,bool shuffle) {
    /* Validate before touching the existing server queue. */
    if(!keys || count<=0 || start<0 || start>=count) return false;
    char quoted_uri[4100];
    for(int i=0;i<count;i++) {
        const char *uri=uri_lookup(keys[i]);
        if(!uri || !mpd_quote(quoted_uri,sizeof(quoted_uri),uri)) {
            message("Selection is stale or has an invalid URI; resync the library"); return true;
        }
    }
    char *batch=malloc(32768); if(!batch) return false;
    if(!simple("command_list_begin\nclear\nrandom 0\nsingle 0\ncommand_list_end")) { free(batch); return false; }
    int index=0;
    bool ok=true;
    while(index<count && ok) {
        size_t used=strlen("command_list_begin\n"); strcpy(batch,"command_list_begin\n");
        for(int n=0;index<count && n<100;n++) {
            const char *uri=uri_lookup(keys[index]); char quoted[4100];
            if(!mpd_quote(quoted,sizeof(quoted),uri)) { ok=false; break; }
            size_t needed=strlen(quoted)+5;
            if(used+needed+32>=32768) break;
            used+=snprintf(batch+used,32768-used,"add %s\n",quoted); index++;
        }
        if(ok) { snprintf(batch+used,32768-used,"command_list_end"); ok=simple(batch); }
        vTaskDelay(1);
    }
    free(batch);
    if(!ok) return false;
    /* Shuffle before starting: enabling random after play pins the first
     * album track in Tracks. The explicit shuffle also randomizes the first
     * song on MPD servers where random affects only the playback sequence. */
    if(shuffle) return simple("command_list_begin\nshuffle\nrandom 1\nplay 0\ncommand_list_end");
    char cmd[40]; snprintf(cmd,sizeof(cmd),"play %d",start);
    return simple(cmd);
}
static bool set_volume(float target) {
    char command[32];
    if(volume_integer_only||player_volume_steps()==2||player_volume_steps()==5)snprintf(command,sizeof(command),"setvol %d",volume_percent(target));
    else {
        // Eight decimal percent digits retain the squared-grid low-volume targets.
        snprintf(command,sizeof(command),"setvol %.8f",target*100.0f);
        char *end=command+strlen(command)-1;
        while(*end=='0')*end--=0;
        if(*end=='.')*end=0;
    }
    if(!simple(command)) {
        // Retry only an explicit invalid-argument ACK, never network failures.
        if(volume_integer_only||!strchr(command,'.')||!strstr(wire.error,"ACK [2@"))return false;
        snprintf(command,sizeof(command),"setvol %d",volume_percent(target));
        if(!simple(command))return false;
        volume_integer_only=true;
    }
    volume_grid_requested(&volume_cursor,target,player_volume_steps());
    return true;
}
static bool run_command(command_t *c) {
    if(c->kind==CMD_COLOPLAY){
        const uint8_t *p=c->coloplay;take();bool current=coloplay_session&&coloplay_session==cp_u32(p+4);give();
        if(!current){c->remote_result=CP_FAILED;return true;}
        const uint16_t usage=cp_u16(p+8),count=cp_u16(p+10),parameter=cp_u16(p+12);
        if(p[1]==CP_RELEASE){take();coloplay_seek=0;give();return true;}
        if(p[1]==CP_VOLUME){
            if(!poll_state())return false;
            if(server_volume<0){c->remote_result=CP_UNSUPPORTED;return true;}
            mpd_state_t latest={0};mpd_client_get_state(&latest);
            // Old controller step fields are ignored; Tab5 owns its native grid.
            const float value=volume_grid_next(&volume_cursor,latest.volume,(usage==0xe9?1:-1)*(int)count,player_volume_steps());
            if(value==latest.volume)c->remote_result=CP_NO_CHANGE;
            return set_volume(value);
        }
        if(usage==0xb3||usage==0xb4){
            const int64_t now=esp_timer_get_time();take();coloplay_seek=usage==0xb3?1:2;coloplay_next_seek=now+1000000;coloplay_seek_deadline=now+(parameter==1?10000000:(int64_t)(cp_u16(p+14)?cp_u16(p+14):200)*1000);give();
            return simple(usage==0xb3?"seekcur +5":"seekcur -5");
        }
        mpd_state_t latest={0};mpd_client_get_state(&latest);
        const char *operation=usage==0xb0?"play":usage==0xb1?"pause 1":usage==0xb7?"stop":usage==0xb5?"next":usage==0xb6?"previous":usage==0xe2?"@mute":usage==0xcd?(latest.play_status==1?"pause 1":"pause 0"):NULL;
        if(!operation){c->remote_result=CP_UNSUPPORTED;return true;}
        if(usage==0xb5||usage==0xb6)for(unsigned n=1;n<count;++n)if(!simple(operation))return false;
        snprintf(c->text,sizeof(c->text),"%s",operation);
    }
    if(c->kind==CMD_STAR) return c->keys && change_star(c->keys[0],false);
    if(c->kind==CMD_CLEAR_STARS) return change_star(0,true);
    if(c->kind==CMD_PLAY) return play_selection(c->keys,c->count,c->start,c->shuffle);
    if(c->kind==CMD_ALL) {
        uint64_t *keys=heap_caps_malloc(uri_count*sizeof(*keys),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!keys || !uri_count) { free(keys); return false; }
        int start=0;
        for(unsigned i=0;i<uri_count;i++) { keys[i]=uris[i].id; if(keys[i]==c->keys[0]) start=i; }
        bool ok=play_selection(keys,uri_count,start,false); free(keys); return ok;
    }
    if(c->kind==CMD_JUMP) {
        char cmd[40]; snprintf(cmd,sizeof(cmd),"playid %lu",(unsigned long)c->queue_id); return simple(cmd);
    }
    mpd_state_t s={0}; mpd_client_get_state(&s);
    char cmd[128];
    if(!strcmp(c->text,"@volume_up") || !strcmp(c->text,"@volume_down")) {
        if(!poll_state())return false;
        mpd_client_get_state(&s);
        if(server_volume<0) { message("MPD mixer is unavailable"); return true; }
        int delta=!strcmp(c->text,"@volume_up") ? 1 : -1;
        return set_volume(volume_grid_next(&volume_cursor,s.volume,delta,player_volume_steps()));
    }
    else if(!strcmp(c->text,"@shuffle")) snprintf(cmd,sizeof(cmd),"random %d",!s.shuffle);
    else if(!strcmp(c->text,"@repeat")) {
        unsigned repeat=(s.repeat==0 ? 2 : s.repeat==2 ? 1 : 0);
        snprintf(cmd,sizeof(cmd),"command_list_begin\nrepeat %u\nsingle %u\ncommand_list_end",repeat>0,repeat==1);
    }
    else if(!strcmp(c->text,"@mute")) {
        if(s.volume<0) { message("MPD mixer is unavailable"); return true; }
        if(s.volume>0) muted_volume=s.volume;
        return set_volume(s.volume > 0 ? 0.0f : volume_grid_snap(muted_volume,player_volume_steps()));
    }
    else snprintf(cmd,sizeof(cmd),"%s",c->text);
    return simple(cmd);
}
static void coloplay_volume_complete(const command_t *c){
    if(c->kind!=CMD_COLOPLAY||c->coloplay[1]!=CP_VOLUME)return;
    take();
    if(coloplay_session==cp_u32(c->coloplay+4))coloplay_volume_completed=cp_u16(c->coloplay+2);
    give();
}
static bool generation_changed(uint32_t generation) { take(); bool changed=generation!=config_revision; give(); return changed; }
static void disconnected(void) {
    mpd_wire_close(&wire);
    mpd_wire_close(&events);
    take(); state.connected=state.identified=false; state.library_syncing=false; state.play_status=0; state.revision++;volume_cursor.active=0;volume_integer_only=false;coloplay_seek=0;
    queue_size=0; queue_revision++; art_valid=false; art_revision++;
    uint64_t *old_stars=star_keys; star_keys=NULL; star_count=0; star_revision++; state.starred=false;
    give(); free(old_stars);
    command_t c; while(xQueueReceive(commands,&c,0)==pdPASS) free(c.keys);
}
/* Tracks implements idle notifications even though its stats db_update is
 * wall-clock time. Keep one dedicated subscription socket; never block input
 * or playback polling while the server is waiting for a change. */
static bool idle_field(const char *key,const char *value,void *ctx)
{
    unsigned *flags=ctx;
    if(!strcmp(key,"changed")) {
        if(!strcmp(value,"database") || !strcmp(value,"update")) *flags|=1;
        if(!strcmp(value,"stored_playlist")) *flags|=2;
    }
    return true;
}
static bool subscribe_events(const mpd_config_t *cfg)
{
    if(!mpd_wire_connect(&events,cfg->host,cfg->port,3000)) return false;
    if(cfg->password[0]) {
        char quoted[260],cmd[280];
        if(!mpd_quote(quoted,sizeof(quoted),cfg->password)) { mpd_wire_close(&events); return false; }
        snprintf(cmd,sizeof(cmd),"password %s",quoted);
        if(!mpd_wire_command(&events,cmd,NULL,NULL,NULL)) { mpd_wire_close(&events); return false; }
    }
    return mpd_wire_send(&events,"idle database stored_playlist update");
}
static unsigned poll_events(void)
{
    unsigned flags=0;
    if(mpd_wire_ready(&events)<=0) return 0;
    if(!mpd_wire_response(&events,idle_field,NULL,&flags)) { mpd_wire_close(&events); return 0; }
    mpd_wire_send(&events,"idle database stored_playlist update");
    return flags;
}

static void worker_task(void *arg) {
    (void)arg;
    for(;;) {
        mpd_config_t cfg; mpd_client_config(&cfg);
        take(); uint32_t generation=config_revision; give();
        if(!cfg.host[0]) { message("Set MPD server in the web interface"); vTaskDelay(pdMS_TO_TICKS(500)); continue; }
        message("Connecting to MPD...");
        if(!mpd_wire_connect(&wire,cfg.host,cfg.port,3000)) {
            message(wire.error); vTaskDelay(pdMS_TO_TICKS(2000)); continue;
        }
        if(cfg.password[0]) {
            char quoted[260],cmd[280];
            if(!mpd_quote(quoted,sizeof(quoted),cfg.password)) { disconnected(); continue; }
            snprintf(cmd,sizeof(cmd),"password %s",quoted);
            if(!simple(cmd)) { message("MPD authentication failed; check web settings"); disconnected(); vTaskDelay(pdMS_TO_TICKS(3000)); continue; }
        }
        unpaged_server=false;
        subscribe_events(&cfg);
        identity(&cfg,library_id,sizeof(library_id));
        player_library_select(library_id);
        /* UI processes the namespace switch before queued database records. */
        bool ok=poll_state() && sync_library() && sync_playlists();

        int loops=0; last_playlist=-1;
        int64_t next_event_retry=esp_timer_get_time()+30000000;
        int64_t next_stars_poll=esp_timer_get_time()+10000000;
        while(ok && !generation_changed(generation)) {
            event_flags=poll_events();
            if(events.fd<0 && esp_timer_get_time()>=next_event_retry) {
                subscribe_events(&cfg);
                next_event_retry=esp_timer_get_time()+30000000;
            }
            command_t c;
            for(int n=0;n<8 && xQueueReceive(commands,&c,0)==pdPASS;n++) {
                if(c.generation==generation) {
                    const bool applied=run_command(&c);
                    coloplay_volume_complete(&c);
                    if(!applied) { message(wire.error[0] ? wire.error : "MPD command failed"); if(wire.fd<0) ok=false; }
                    /* Poll after each action so successive toggles use live state. */
                    if(ok) ok=poll_state();
                    if(c.kind==CMD_COLOPLAY)coloplay_ble_result(c.coloplay,applied&&ok?c.remote_result:CP_FAILED);
                }
                free(c.keys);
                if(!ok) break;
            }
            if(!ok || !poll_state()) { ok=false; break; }
            if(mpd_playlist_version!=last_playlist || queue_refresh_pending) {
                if(reported_queue_size>QUEUE_LIMIT) {
                    message("Queue exceeds 24000 entries; showing current track");
                    take(); queue_size=0; queue_revision++; give();
                    queue_refresh_pending=false;
                }
                else if(!sync_queue()) { ok=false; break; }
                last_playlist=mpd_playlist_version;
            }
            take(); bool requested=rescan; rescan=false; uint8_t held=hold_bit; give();
            if(held) simple(held==11 ? "seekcur -5" : "seekcur +5");
            /* Dedicated ColoPlay seek lease; USB/D100H hold state is untouched. */
            take();const int64_t seek_now=esp_timer_get_time();if(seek_now>=coloplay_seek_deadline)coloplay_seek=0;
            const uint8_t remote_seek=!held&&seek_now>=coloplay_next_seek?coloplay_seek:0;if(remote_seek)coloplay_next_seek=seek_now+1000000;give();
            if(remote_seek){simple(remote_seek==1?"seekcur +5":"seekcur -5");poll_state();}
            if(requested || (event_flags&1)) {
                ok=sync_library() && sync_playlists(); last_playlist=-1;
            } else if(event_flags&2) ok=sync_playlists();
            else if(stars_refresh_pending) ok=sync_stars();
            /* Tracks UI edits do not always emit stored_playlist idle events. */
            if(ok && esp_timer_get_time()>=next_stars_poll) {
                ok=sync_stars();
                next_stars_poll=esp_timer_get_time()+10000000;
            }
            if(ok && loops++%30==0) {
                char db[64]={0}; unsigned previous_songs=database_songs;
                if(!mpd_wire_command(&wire,"stats",stat_field,NULL,db)) { ok=false; break; }
                bool changed=previous_songs!=database_songs;
                /* Generic MPD fallback when idle is unavailable. Tracks'
                 * unpaged server must never reimport on its rolling timestamp. */
                if(events.fd<0 && !unpaged_server && database_revision[0] && strcmp(database_revision,db)) changed=true;
                if(changed) { ok=sync_library() && sync_playlists(); last_playlist=-1; }
                snprintf(database_revision,sizeof(database_revision),"%s",db);
                if(mpd_updating) message("MPD is updating its database");
                else if(ok) message("MPD connected");
            }
            for(int n=0;n<10 && !generation_changed(generation);n++) {
                if(uxQueueMessagesWaiting(commands)) break;
                artwork_step(generation);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        if(!ok) { message(wire.error[0] ? wire.error : "MPD sync failed; retrying"); ESP_LOGW(TAG,"%s",status_text); }
        disconnected();
        if(!generation_changed(generation)) vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
esp_err_t mpd_client_start(void) {
    if(worker) return ESP_OK;
    uris=heap_caps_calloc(TRACK_LIMIT,sizeof(*uris),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uri_slots=heap_caps_calloc(URI_SLOTS,sizeof(*uri_slots),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    queue_ids=heap_caps_calloc(QUEUE_LIMIT,sizeof(*queue_ids),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    queue_keys=heap_caps_calloc(QUEUE_LIMIT,sizeof(*queue_keys),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    art=heap_caps_malloc(ART_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!uris || !uri_slots || !queue_ids || !queue_keys || !art) { message("Not enough memory for MPD"); return ESP_ERR_NO_MEM; }
    if(!artwork_start()) return ESP_ERR_NO_MEM;
    if(xTaskCreateWithCaps(worker_task,"mpd",32768,NULL,3,&worker,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
