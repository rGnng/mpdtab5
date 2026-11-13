"""Exercise the firmware artwork scheduler and transfers with a clock/socket peer.

RTOS queues and JPEG decoding are boundaries; production scheduling, streaming,
cache file publication and generation checks are compiled unchanged.
"""
import ctypes as C
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import unittest
from test_album_view import function

ROOT=Path(__file__).resolve().parents[1]
PREFIX=r'''
#include "mpd_wire.h"
#include "mpd_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define ART_BYTES (360*360*2)
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define heap_caps_malloc(n,c) malloc(n)
#define heap_caps_calloc(n,s,c) calloc(n,s)
#define portMAX_DELAY 0
#define pdPASS 1
#define ESP_LOGI(...) ((void)0)
typedef struct { size_t size; int ready; unsigned char data[5000]; } queue_t;
typedef queue_t *QueueHandle_t;
static QueueHandle_t xQueueCreate(int n,size_t size) { (void)n; queue_t *q=calloc(1,sizeof(*q)); q->size=size; return q; }
static int xQueueSend(QueueHandle_t q,const void *p,int wait) { (void)wait; if(q->ready)return 0;memcpy(q->data,p,q->size);q->ready=1;return 1; }
static int xQueueReceive(QueueHandle_t q,void *p,int wait) { (void)wait;if(!q->ready)return 0;memcpy(p,q->data,q->size);q->ready=0;return 1; }
#define xTaskCreateWithCaps(...) 1
static int mutex=1;
static uint32_t config_revision=1, art_revision;
static uint8_t art[ART_BYTES];
static bool art_valid;
static mpd_state_t state;
static char current_uri[2048]="current.mp3", library_id[64]="MPD-test", directory[192];
static int64_t clock_us;
static unsigned captures;
static uint64_t cached_pid;
static void take(void) {}
static void give(void) {}
static int64_t esp_timer_get_time(void) { return clock_us; }
static bool app_config_sd_ready(void) { return true; }
static const char *app_config_artwork_dir(void) { return directory; }
static const char *app_config_library_id(void) { return library_id; }
bool mpd_client_get_state(mpd_state_t *s) { *s=state; return true; }
void mpd_client_config(mpd_config_t *c) { memset(c,0,sizeof(*c));strcpy(c->host,"127.0.0.1"); }
static const char *uri_lookup(uint64_t id) { (void)id;return "representative.mp3"; }
static uint32_t thumb_cache_slot(uint64_t pid,bool create) { (void)create;return cached_pid==pid ? 1 : 0; }
static bool thumb_cache_has(uint32_t slot) { return slot==1; }
static bool thumb_cache_capture(const char *library,uint64_t pid,const uint8_t *pixels) {
    (void)pixels;if(strcmp(library,library_id))return false;cached_pid=pid;captures++;return true;
}
static bool jpeg_thumbnail_decode(const char *path,uint8_t *pixels,unsigned size) {
    (void)size;FILE *f=fopen(path,"rb");char data[4];bool ok=f && fread(data,1,4,f)==4 && !memcmp(data,"jpeg",4);if(f)fclose(f);
    if(ok)memset(pixels,17,ART_BYTES);
    return ok;
}
'''
SUFFIX=r'''
void test_init(const char *path) {
    if(!art_albums) artwork_start();
    strcpy(directory,path);state=(mpd_state_t){.connected=true};
    art_jobs->ready=art_results->ready=0;art_busy=false;
    art_activity=clock_us=0;art_visible_count=0;art_current_key=art_attempted_key=0;
    art_current_retry=0;cached_pid=0;captures=0;art_valid=false;
    config_revision=1;artwork_reset();
}
void test_add(uint64_t album,uint64_t track) { artwork_register(album,track); }
void test_clock(int64_t now) { clock_us=now; }
void test_visible(uint64_t album,int active) { mpd_client_artwork_view(&album,album?1:0,active); }
void test_playing(uint64_t album,uint64_t track) { state.album_pid=album;state.track_key=track; }
void test_cached(uint64_t pid) { cached_pid=pid; }
void test_step(int64_t now) { clock_us=now;artwork_step(config_revision); }
uint64_t test_job_album(void) { return art_jobs->ready ? ((art_job_t*)art_jobs->data)->album : 0; }
int test_job_current(void) { return art_jobs->ready && ((art_job_t*)art_jobs->data)->now_playing; }
void test_complete(int result) {
    art_job_t job; if(!xQueueReceive(art_jobs,&job,0))abort();
    art_result_t r={.album=job.album,.track=job.track,.catalog=job.catalog,.now_playing=job.now_playing,.result=result};
    xQueueSend(art_results,&r,0);
}
unsigned test_attempts(void) { return art_albums[0].attempts; }
void test_change_generation(void) { config_revision++; }
void test_reset(void) { artwork_reset(); }
int test_fetch(int port,uint64_t album,uint64_t track,int stale,int current) {
    art_job_t job={.album=album,.track=track,.generation=config_revision-stale,.catalog=art_catalog,.now_playing=current};
    strcpy(job.uri,"a \\\" song.mp3");strcpy(job.library,library_id);strcpy(job.directory,directory);
    strcpy(job.server.host,"127.0.0.1");job.server.port=port;
    mpd_wire_t conn;mpd_wire_init(&conn);uint8_t *pixels=malloc(ART_BYTES);
    int result=artwork_fetch(&job,&conn,pixels);free(pixels);mpd_wire_close(&conn);return result;
}
int test_valid(void) { return art_valid; }
unsigned test_captures(void) { return captures; }
const char *test_progress_state(void) { mpd_artwork_status_t s;mpd_client_artwork_status(&s);return s.status; }
unsigned test_progress_checked(void) { mpd_artwork_status_t s;mpd_client_artwork_status(&s);return s.checked; }
unsigned test_progress_missing(void) { mpd_artwork_status_t s;mpd_client_artwork_status(&s);return s.missing; }

'''


class ArtworkTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory();root=Path(cls.build.name)
        (root/'esp_err.h').write_text('typedef int esp_err_t;\n')
        source=(ROOT/'firmware/main/mpd_client.c').read_text()
        transfer='typedef struct { FILE *file; unsigned total, received, chunk; } art_transfer_t;\n'
        transfer+='\n'.join(function(source,n) for n in ['art_field','art_binary'])
        (root/'art.c').write_text(PREFIX+transfer+'\n#include "mpd_artwork.inc"\n'+SUFFIX)
        subprocess.run(['cc','-std=gnu11','-shared','-fPIC','-Wall','-Wextra','-Wno-unused-function',
                        '-Wno-format-truncation','-I'+str(root),'-I'+str(ROOT/'app/mpd'),
                        '-I'+str(ROOT/'app/backend'),'-I'+str(ROOT/'firmware/main'),
                        str(root/'art.c'),str(ROOT/'app/mpd/mpd_wire.c'),'-o',str(root/'art.so')],check=True)
        cls.lib=C.CDLL(str(root/'art.so'))
        cls.lib.test_init.argtypes=[C.c_char_p]
        for name in ['test_clock','test_step']: getattr(cls.lib,name).argtypes=[C.c_int64]
        cls.lib.test_add.argtypes=cls.lib.test_playing.argtypes=[C.c_uint64,C.c_uint64]
        cls.lib.test_visible.argtypes=[C.c_uint64,C.c_int]
        cls.lib.test_job_album.restype=C.c_uint64
        cls.lib.test_progress_state.restype=C.c_char_p
        cls.lib.test_fetch.argtypes=[C.c_int,C.c_uint64,C.c_uint64,C.c_int,C.c_int]

    @classmethod
    def tearDownClass(cls): cls.build.cleanup()

    def setUp(self):
        self.cache=tempfile.TemporaryDirectory();self.addCleanup(self.cache.cleanup)
        self.lib.test_init(self.cache.name.encode())

    def test_visible_priority_starts_after_scroll_stops(self):
        self.lib.test_add(10,100);self.lib.test_add(20,200)
        self.lib.test_clock(3000000);self.lib.test_visible(20,1)
        self.lib.test_step(3139999);self.assertEqual(self.lib.test_job_album(),0)
        self.lib.test_visible(20,0);self.lib.test_step(3140000)
        self.assertEqual(self.lib.test_job_album(),20)

    def test_idle_pass_waits_three_seconds_and_skips_cached(self):
        self.lib.test_add(10,100);self.lib.test_add(20,200);self.lib.test_cached(10)
        self.lib.test_step(2999999);self.assertEqual(self.lib.test_job_album(),0)
        self.lib.test_step(3000000);self.assertEqual(self.lib.test_job_album(),20)

    def test_now_playing_has_priority_and_only_one_job_is_in_flight(self):
        self.lib.test_add(10,100);self.lib.test_visible(10,0);self.lib.test_playing(30,300)
        self.lib.test_step(3000000);self.assertEqual(self.lib.test_job_album(),30)
        self.assertTrue(self.lib.test_job_current())
        self.lib.test_step(4000000);self.assertEqual(self.lib.test_job_album(),30)
        self.lib.test_complete(1);self.lib.test_step(4100000)
        self.assertEqual(self.lib.test_job_album(),10)

    def test_scrolling_cancellation_does_not_exhaust_retries(self):
        self.lib.test_add(10,100);self.lib.test_visible(10,0)
        for i in range(5):
            self.lib.test_step(3000000+i*1000000);self.assertEqual(self.lib.test_job_album(),10)
            self.lib.test_complete(-1)
            self.lib.test_step(3100000+i*1000000)
        self.assertEqual(self.lib.test_attempts(),0)

    def test_transient_failures_back_off_and_absent_covers_are_skipped(self):
        self.lib.test_add(10,100);self.lib.test_visible(10,0)
        self.lib.test_step(3000000);self.lib.test_complete(0);self.lib.test_step(3100000)
        self.assertEqual(self.lib.test_attempts(),1);self.assertEqual(self.lib.test_job_album(),0)
        self.lib.test_step(63100000);self.assertEqual(self.lib.test_job_album(),10)
        self.lib.test_complete(2);self.lib.test_step(64000000)
        self.assertEqual(self.lib.test_job_album(),0)
        self.lib.test_step(200000000);self.assertEqual(self.lib.test_job_album(),0)
        self.lib.test_reset();self.lib.test_add(10,100);self.lib.test_step(201000000)
        self.assertEqual(self.lib.test_job_album(),10)

    def test_progress_reports_paused_active_and_completed_with_missing(self):
        self.lib.test_add(10,100)
        self.lib.test_step(3000000);self.lib.test_step(3000001)
        self.assertEqual(self.lib.test_progress_state(),b'caching')
        self.lib.test_visible(0,1)
        self.assertEqual(self.lib.test_progress_state(),b'paused')
        self.lib.test_complete(2);self.lib.test_step(4000000)
        self.assertEqual(self.lib.test_progress_state(),b'complete')
        self.assertEqual(self.lib.test_progress_checked(),1)
        self.assertEqual(self.lib.test_progress_missing(),1)

    def peer(self,replies):
        server=socket.socket();server.bind(('127.0.0.1',0));server.listen();server.settimeout(3)
        errors=[];commands=[]
        def run():
            try:
                conn,_=server.accept()
                with conn:
                    conn.settimeout(3);conn.sendall(b'OK MPD 0.24.0\n');stream=conn.makefile('rb')
                    for reply in replies:
                        commands.append(stream.readline().decode().rstrip())
                        if callable(reply): reply=reply()
                        conn.sendall(reply)
            except Exception as exc: errors.append(exc)
            finally: server.close()
        thread=threading.Thread(target=run);thread.start()
        return server.getsockname()[1],thread,commands,errors

    def test_binary_fallback_persists_full_cover_and_warm_read_uses_no_connection(self):
        self.lib.test_clock(3000000)
        port,thread,commands,errors=self.peer([b'OK\n',b'ACK [50@0] {readpicture} no art\n',
            b'size: 8\ntype: image/jpeg\nbinary: 4\njpeg\nOK\n',b'size: 8\nbinary: 4\ndata\nOK\n'])
        self.assertEqual(self.lib.test_fetch(port,10,100,0,0),1);thread.join();self.assertFalse(errors)
        self.assertTrue(commands[1].startswith('readpicture "'))
        self.assertTrue(commands[2].startswith('albumart "'));self.assertTrue(commands[3].endswith(' 4'))
        self.assertEqual((Path(self.cache.name)/'10.cov').read_bytes(),b'\x11'*(360*360*2))
        self.assertEqual(self.lib.test_fetch(1,10,100,0,0),1)
        self.assertEqual(self.lib.test_captures(),2)
        self.assertFalse(self.lib.test_valid())  # Background albums never replace Now Playing.
        self.assertFalse(list(Path(self.cache.name).glob('*.tmp')))

    def test_activity_between_chunks_cancels_without_publishing_partial_file(self):
        self.lib.test_clock(3000000)
        def scroll():
            self.lib.test_visible(0,1)
            return b'size: 8\nbinary: 4\njpeg\nOK\n'
        port,thread,commands,errors=self.peer([b'OK\n',scroll])
        self.assertEqual(self.lib.test_fetch(port,10,100,0,0),-1);thread.join();self.assertFalse(errors)
        self.assertEqual(self.lib.test_captures(),0);self.assertFalse(list(Path(self.cache.name).iterdir()))

    def test_stale_server_and_stale_playing_track_never_publish(self):
        self.lib.test_clock(3000000);self.lib.test_playing(20,200)
        self.assertEqual(self.lib.test_fetch(1,10,100,1,0),-1)
        self.assertEqual(self.lib.test_fetch(1,10,100,0,1),-1)
        self.assertFalse(self.lib.test_valid());self.assertEqual(self.lib.test_captures(),0)

if __name__=='__main__': unittest.main()
