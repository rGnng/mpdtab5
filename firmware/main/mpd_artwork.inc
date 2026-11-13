/* Included by mpd_client.c. The control worker owns the album index and URI
 * store. A separate, single artwork worker receives copied identities/URIs;
 * artwork never occupies the control connection or blocks the LVGL thread. */
#define ART_ALBUM_LIMIT 4096
#define ART_VISIBLE_LIMIT 10
#define ART_SCROLL_IDLE_US 140000
#define ART_BACKGROUND_IDLE_US 3000000

typedef struct {
    uint64_t album, track;
    int64_t retry;
    unsigned attempts;
    bool done;
    unsigned outcome; /* 1 cached, 2 absent/unsupported, 3 retries exhausted */
} art_album_t;
typedef struct {
    uint64_t album, track;
    uint32_t generation, catalog;
    bool now_playing;
    char uri[2048], library[64], directory[96];
    mpd_config_t server;
} art_job_t;
typedef struct {
    uint64_t album, track;
    uint32_t catalog;
    int result; /* 1 cached, 2 absent/unsupported, 0 transport error, -1 canceled */
    bool now_playing;
} art_result_t;
static art_album_t *art_albums;
static unsigned art_album_count, art_cursor;
static uint32_t art_catalog=1;
static QueueHandle_t art_jobs, art_results;
static bool art_busy;
static art_job_t *art_worker_job;
static uint8_t *art_worker_pixels;
static uint64_t art_visible[ART_VISIBLE_LIMIT], art_current_key, art_attempted_key;
static unsigned art_visible_count;
static int64_t art_activity;
static int64_t art_current_retry;
static mpd_artwork_status_t art_progress;

void mpd_client_artwork_status(mpd_artwork_status_t *out)
{
    take();
    *out=art_progress;
    int64_t idle=esp_timer_get_time()-art_activity;
    if(!state.connected) out->status="offline";
    else if(state.library_syncing || !out->total) out->status="library";
    else if(out->checked>=out->total) out->status="complete";
    else if(idle<ART_SCROLL_IDLE_US) out->status="paused";
    else if(out->active) out->status="caching";
    else if(idle<ART_BACKGROUND_IDLE_US) out->status="waiting";
    else out->status="retrying";
    give();
    if(!app_config_sd_ready()) out->status="no_sd";
}
static void artwork_progress_update(void)
{
    mpd_artwork_status_t next={.total=art_album_count,.active=art_busy};
    for(unsigned i=0;i<art_album_count;i++) {
        next.checked+=art_albums[i].done;
        next.missing+=art_albums[i].outcome==2;
        next.failed+=art_albums[i].outcome==3;
    }
    take(); art_progress=next; give();
}

void mpd_client_artwork_pause(void)
{
    if(!mutex) return;
    take(); art_activity=esp_timer_get_time(); art_visible_count=0; give();
}
void mpd_client_artwork_view(const uint64_t *albums, unsigned count, bool active)
{
    if(!mutex) return;
    take();
    if(active) { art_activity=esp_timer_get_time(); art_visible_count=0; }
    else {
        art_visible_count=count<ART_VISIBLE_LIMIT ? count : ART_VISIBLE_LIMIT;
        if(art_visible_count) memcpy(art_visible,albums,art_visible_count*sizeof(*albums));
    }
    give();
}
static bool artwork_allowed(const art_job_t *job)
{
    take();
    bool ok=job->generation==config_revision && job->catalog==art_catalog &&
        esp_timer_get_time()-art_activity>=ART_SCROLL_IDLE_US &&
        (!job->now_playing || state.track_key==job->track);
    give();
    return ok;
}
static void artwork_register(uint64_t album, uint64_t track)
{
    if(!art_albums || !album || !track) return;
    for(unsigned i=0;i<art_album_count;i++) if(art_albums[i].album==album) return;
    if(art_album_count<ART_ALBUM_LIMIT)
        art_albums[art_album_count++]=(art_album_t){.album=album,.track=track};
}
static void artwork_reset(void)
{
    take(); art_catalog++; memset(&art_progress,0,sizeof(art_progress)); give();
    art_album_count=art_cursor=0;
    art_attempted_key=0;
    art_current_retry=0;
}
static bool artwork_publish(const art_job_t *job, const uint8_t *pixels)
{
    take();
    bool valid=job->generation==config_revision && job->catalog==art_catalog;
    if(valid && job->now_playing && state.track_key==job->track) {
        memcpy(art,pixels,ART_BYTES); art_valid=true; art_revision++;
    }
    give();
    return valid;
}
static int artwork_fetch(const art_job_t *job, mpd_wire_t *connection, uint8_t *pixels)
{
    if(!artwork_allowed(job)) return -1;
    char path[192], temporary[200];
    /* Full covers use album PID names; UI obtains that PID from the thumbnail
     * map. Both formats live in the existing per-server SD artwork directory. */
    snprintf(path,sizeof(path),"%s/%llu.cov",job->directory,(unsigned long long)job->album);
    FILE *cached=fopen(path,"rb");
    if(cached) {
        bool ok=fread(pixels,1,ART_BYTES,cached)==ART_BYTES && fgetc(cached)==EOF;
        fclose(cached);
        if(ok && artwork_allowed(job) && artwork_publish(job,pixels))
            return thumb_cache_capture(job->library,job->album,pixels) ? 1 : 0;
    }
    if(!artwork_allowed(job)) return -1;
    if(!mpd_wire_connect(connection,job->server.host,job->server.port,5000)) return 0;
    char quoted[4100],cmd[4160];
    if(job->server.password[0]) {
        mpd_quote(quoted,sizeof(quoted),job->server.password);
        snprintf(cmd,sizeof(cmd),"password %s",quoted);
        if(!mpd_wire_command(connection,cmd,NULL,NULL,NULL)) return 0;
    }
    /* Bound each chunk so a newly started scroll can interrupt between chunks. */
    mpd_wire_command(connection,"binarylimit 16384",NULL,NULL,NULL);
    if(!mpd_quote(quoted,sizeof(quoted),job->uri)) return 2;
    snprintf(temporary,sizeof(temporary),"%s/art-download.tmp",job->directory);
    bool complete=false, absent=true;
    for(int mode=0;mode<2 && connection->fd>=0 && !complete;mode++) {
        if(!artwork_allowed(job)) { absent=false; break; }
        FILE *f=fopen(temporary,"wb"); if(!f) return 0;
        art_transfer_t transfer={.file=f};
        do {
            if(!artwork_allowed(job)) { absent=false; break; }
            transfer.chunk=0;
            snprintf(cmd,sizeof(cmd),"%s %s %u",mode ? "albumart" : "readpicture",quoted,transfer.received);
            bool ok=mpd_wire_command(connection,cmd,art_field,art_binary,&transfer);
            if(!ok) {
                if(connection->fd<0 || !strstr(connection->error,"ACK [50@")) absent=false;
                break;
            }
            if(transfer.total && transfer.received==transfer.total) { complete=true; break; }
        } while(transfer.chunk);
        if(fclose(f)!=0) { complete=false; absent=false; }
    }
    int result=absent ? 2 : 0;
    if(complete && artwork_allowed(job)) {
        if(jpeg_thumbnail_decode(temporary,pixels,360)) {
            if(artwork_allowed(job)) {
                char pending[200]; snprintf(pending,sizeof(pending),"%s.tmp",path);
                FILE *f=fopen(pending,"wb");
                bool saved=false;
                if(f) {
                    saved=fwrite(pixels,1,ART_BYTES,f)==ART_BYTES;
                    saved=fclose(f)==0 && saved;
                }
                if(saved && artwork_allowed(job)) saved=rename(pending,path)==0;
                else saved=false;
                remove(pending);
                if(saved && artwork_publish(job,pixels))
                    result=thumb_cache_capture(job->library,job->album,pixels) ? 1 : 0;
                else result=0;
            } else result=0;
        } else result=2; /* Unsupported/corrupt image: skip until the next rescan. */
    } else if(complete) result=0;
    remove(temporary);
    return artwork_allowed(job) ? result : -1;
}
static void artwork_task(void *unused)
{
    (void)unused;
    art_job_t *job=art_worker_job;
    uint8_t *pixels=art_worker_pixels;
    for(;;) {
        if(xQueueReceive(art_jobs,job,portMAX_DELAY)!=pdPASS) continue;
        mpd_wire_t connection; mpd_wire_init(&connection);
        art_result_t result={.album=job->album,.track=job->track,.catalog=job->catalog,
            .now_playing=job->now_playing};
        result.result=artwork_fetch(job,&connection,pixels);
        mpd_wire_close(&connection);
        if(result.result==1) ESP_LOGI(TAG,"cached album artwork %016llX",(unsigned long long)job->album);
        xQueueSend(art_results,&result,portMAX_DELAY);
    }
}
static bool artwork_cached(art_album_t *album)
{
    uint32_t slot=thumb_cache_slot(album->album,false);
    if(slot && thumb_cache_has(slot)) { album->done=true; album->outcome=1; }
    return album->done;
}
static void artwork_step(uint32_t generation)
{
    if(!art_jobs || !app_config_sd_ready()) return;
    if(strcmp(library_id,app_config_library_id())) return;
    int64_t now=esp_timer_get_time();
    art_result_t result;
    if(xQueueReceive(art_results,&result,0)==pdPASS) {
        art_busy=false;
        if(result.catalog==art_catalog) {
            for(unsigned i=0;i<art_album_count;i++) if(art_albums[i].album==result.album) {
                art_album_t *a=&art_albums[i];
                a->done=result.result>0;
                a->outcome=result.result>0 ? result.result : 0;
                a->retry=now+(result.result<0 ? ART_SCROLL_IDLE_US : 60000000);
                if(!result.result && ++a->attempts>=3) { a->done=true; a->outcome=3; }
                break;
            }
            if(result.now_playing && result.track==art_current_key) {
                if(result.result>0) art_attempted_key=result.track;
                art_current_retry=now+(result.result<0 ? ART_SCROLL_IDLE_US : 3000000);
            }
        }
    }
    artwork_progress_update();
    mpd_state_t playing={0}; mpd_client_get_state(&playing);
    if(playing.track_key!=art_current_key) {
        take(); art_valid=false; art_revision++; give();
        art_current_key=playing.track_key; art_attempted_key=0; art_current_retry=0;
    }
    if(art_busy || !playing.connected || playing.library_syncing) return;
    uint64_t visible[ART_VISIBLE_LIMIT]; unsigned count; int64_t idle;
    take(); count=art_visible_count; memcpy(visible,art_visible,count*sizeof(*visible)); idle=now-art_activity; give();
    if(idle<ART_SCROLL_IDLE_US) return;
    uint64_t album=0, track=0;
    bool current=playing.track_key && playing.album_pid && art_attempted_key!=playing.track_key && now>=art_current_retry;
    if(current) { album=playing.album_pid; track=playing.track_key; }
    else {
        for(unsigned v=0;v<count && !album;v++)
            for(unsigned i=0;i<art_album_count;i++) {
                art_album_t *a=&art_albums[i];
                if(a->album==visible[v] && !a->done && now>=a->retry && !artwork_cached(a)) {
                    album=a->album; track=a->track; break;
                }
            }
        if(!album && idle>=ART_BACKGROUND_IDLE_US) {
            /* Bound cache scans even after a warm boot with thousands of hits. */
            for(unsigned n=0;n<64 && art_album_count;n++) {
                art_album_t *a=&art_albums[art_cursor++%art_album_count];
                if(!a->done && now>=a->retry && !artwork_cached(a)) {
                    album=a->album; track=a->track; break;
                }
            }
        }
    }
    if(!album) return;
    const char *uri=current ? current_uri : uri_lookup(track);
    if(!uri || !uri[0]) return;
    art_job_t *job=calloc(1,sizeof(*job)); if(!job) return;
    job->album=album; job->track=track; job->generation=generation;
    job->catalog=art_catalog; job->now_playing=current;
    snprintf(job->uri,sizeof(job->uri),"%s",uri);
    snprintf(job->library,sizeof(job->library),"%s",library_id);
    snprintf(job->directory,sizeof(job->directory),"%s",app_config_artwork_dir());
    mpd_client_config(&job->server);
    if(xQueueSend(art_jobs,job,0)==pdPASS) art_busy=true;
    free(job);
}
static bool artwork_start(void)
{
    art_albums=heap_caps_calloc(ART_ALBUM_LIMIT,sizeof(*art_albums),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    art_jobs=xQueueCreate(1,sizeof(art_job_t));
    art_results=xQueueCreate(1,sizeof(art_result_t));
    art_worker_job=malloc(sizeof(*art_worker_job));
    art_worker_pixels=heap_caps_malloc(ART_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    return art_albums && art_jobs && art_results && art_worker_job && art_worker_pixels &&
        xTaskCreateWithCaps(artwork_task,"mpd-art",24576,NULL,2,NULL,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS;
}
