"""Run production MPD metadata/queue/control functions against a socket peer.

Only the RTOS, display and persistence boundaries are replaced. The source
functions and the socket protocol implementation are compiled as written.
"""
import ctypes as C
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import threading
import unittest
from test_album_view import function

ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'firmware/main/mpd_client.c').read_text()

PREFIX=r'''
#include "mpd_wire.h"
#include "mpd_client.h"
#include "player.h"
#include "text_util.h"
#include "volume_grid.h"
#include "coloplay_ble_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#define TRACK_LIMIT 24000
#define QUEUE_LIMIT 24000
#define URI_BYTES (3*1024*1024)
#define URI_SLOTS 65536
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
static size_t allocation_limit=SIZE_MAX, largest_allocation;
static void *test_allocate(size_t size) {
    if(size>largest_allocation) largest_allocation=size;
    return size>allocation_limit ? NULL : malloc(size);
}
#define heap_caps_malloc(n,caps) test_allocate(n)
static void *test_reallocate(void *ptr,size_t n) {
    if(n>largest_allocation) largest_allocation=n;
    return n>allocation_limit ? NULL : realloc(ptr,n);
}
#define heap_caps_realloc(p,n,caps) test_reallocate(p,n)
#define heap_caps_get_free_size(caps) 0
#define heap_caps_get_largest_free_block(caps) 0
#define ESP_LOGW(...) ((void)0)
#define vTaskDelay(n) ((void)(n))
static mpd_wire_t wire;
static mpd_state_t state;
static float muted_volume=0.5f;
static volume_grid_cursor_t volume_cursor;
static uint16_t coloplay_volume_completed;
static float server_volume=-1.0f;
static bool volume_integer_only=false;
static int mpd_playlist_version;
static mpd_config_t config;
static char current_uri[4096];
static uint32_t coloplay_epoch;static uint32_t coloplay_session=7;static uint8_t coloplay_seek;static int64_t coloplay_seek_deadline,coloplay_next_seek;
static int64_t esp_timer_get_time(void) { return 1000000; }
static bool remote_repeat,remote_single,mpd_updating;
static unsigned reported_queue_size, database_songs;
static char database_revision[64];
static bool unpaged_server;
static int volume_steps=16;
int player_volume_steps(void) { return volume_steps; }
static player_media_item_t items[1000];
static int item_count,playlist_count,complete_count;
static uint64_t playlist_keys[1024];
static uint32_t parsed_queue_ids[QUEUE_LIMIT];
static uint64_t queue_keys[QUEUE_LIMIT];
static uint32_t queue_ids[QUEUE_LIMIT], queue_revision;
static int queue_size;
#define QUEUE_PAGE_SIZE 64
static uint8_t queue_pages[(QUEUE_LIMIT+QUEUE_PAGE_SIZE-1)/QUEUE_PAGE_SIZE];
static int queue_requests,queue_cached,queue_cache_version=-1;
static uint32_t queue_current_id;
static uint32_t queue_epoch=1;
static int queue_priority_support=-1;
static uint32_t *queue_next_ids;
static int queue_next_count;
static uint32_t queue_next_head;
static bool queue_next_head_valid;
static bool queue_next_tick(uint32_t current,bool valid,bool random);
static void queue_next_queue_changed(void);
static bool queue_refresh_pending,stars_refresh_pending;
static uint64_t *star_keys;
static int star_count;
static uint32_t star_revision;
static char playlist_name[256];
static char diagnostic[200];
static void take(void) {}
static void give(void) {}
void player_library_reset(void) { item_count=0; }
void player_library_complete(void) { complete_count++; }
static void message(const char *text) { snprintf(diagnostic,sizeof(diagnostic),"%s",text); }
bool mpd_client_get_state(mpd_state_t *out) { *out=state; return true; }
void player_library_update(const player_media_item_t *item) { if(item_count<1000) items[item_count++]=*item; }
void player_library_playlists_reset(void) { playlist_count=0; }
void player_library_playlist_item(int index,const char *name,uint64_t key) {
    if(name) snprintf(playlist_name,sizeof(playlist_name),"%s",name);
    (void)index; (void)name;
    if(playlist_count<1024) playlist_keys[playlist_count++]=key;
}
'''
SUFFIX=r'''
unsigned test_publish(int elapsed,int level,int starred,const char *title) {
    mpd_state_t next=state; next.elapsed_ms=elapsed; next.volume=level;
    next.starred=starred; snprintf(next.title,sizeof(next.title),"%s",title);
    publish(&next); return state.revision;
}
unsigned test_elapsed(void) { return state.elapsed_ms; }
void test_init(int fd) {
    memset(uri_slots,0,sizeof(uri_slots)); uri_count=uri_used=0;
    memset(&state,0,sizeof(state)); item_count=playlist_count=0;
    unpaged_server=false; database_songs=0;
    allocation_limit=SIZE_MAX;largest_allocation=0;queue_size=0;queue_refresh_pending=false;
    free(star_keys);star_keys=NULL;star_count=0;star_revision=0;stars_refresh_pending=false;
    coloplay_volume_completed=0;volume_steps=16;memset(&volume_cursor,0,sizeof(volume_cursor));server_volume=-1;volume_integer_only=false;muted_volume=0.5f;
    complete_count=0;mpd_playlist_version=1;queue_cache_version=-1;queue_requests=queue_cached=0;
    queue_current_id=0;state.queue_index=-1;reported_queue_size=0;
    memset(queue_keys,0,sizeof(queue_keys));memset(queue_pages,0,sizeof(queue_pages));
    free(queue_next_ids);queue_next_ids=NULL;queue_next_count=0;queue_next_head_valid=false;queue_priority_support=-1;queue_epoch=1;
    free(playlist_destinations);playlist_destinations=NULL;playlist_destination_generation=0;playlists_refresh_pending=false;playlist_connection_generation=config_revision=1;
    diagnostic[0]=0; mpd_wire_init(&wire); mpd_wire_attach(&wire,fd,1000);
}
int test_sync_queue(unsigned size,unsigned budget) {
    reported_queue_size=size; allocation_limit=budget; largest_allocation=0;
    return sync_queue();
}
int test_page(void){return load_queue_page();}
int test_cached(void){return queue_cached;}
unsigned test_queue_revision(void){return queue_revision;}
int test_requests(void){return queue_requests;}
int test_version(void){return mpd_playlist_version;}
void test_seed(int index,uint64_t key,unsigned id){state.queue_index=index;state.track_key=key;queue_current_id=id;}
unsigned test_published_queue_id(int i){return queue_ids[i];}
unsigned test_allocation(void) { return largest_allocation; }
int test_queue_count(void) { return queue_size; }
int test_completions(void) { return complete_count; }
int test_queue_pending(void) { return queue_refresh_pending; }
int test_sync_stars(unsigned budget) { allocation_limit=budget;return sync_stars(); }
int test_sync_playlists(void) { return sync_playlists(); }
int test_playlist_add(const char *name,uint64_t key,unsigned captured,unsigned queued){
    command_t c={.kind=CMD_PLAYLIST_ADD,.keys=&key,.playlist=(char*)name,.playlist_generation=captured,.generation=queued};
    return run_command(&c);
}
int test_playlist_remove(const char *name,uint64_t *keys,int count,int position,unsigned captured,unsigned queued){
    command_t c={.kind=CMD_PLAYLIST_REMOVE,.keys=keys,.count=count,.start=position,.playlist=(char*)name,.playlist_generation=captured,.generation=queued};
    return run_command(&c);
}
int test_shuffle_all(void){command_t c={.kind=CMD_ALL,.shuffle=true};return run_command(&c);}
int test_playlists_pending(void){return playlists_refresh_pending;}
int test_destination_count(unsigned revision,int connected){config_revision=revision;state.connected=connected;return mpd_client_playlist_destination_count();}
const char *test_destination_name(int index){static char name[256];uint32_t generation=0;return mpd_client_playlist_destination(index,name,sizeof(name),&generation)?name:NULL;}

int test_star_count(void) { return star_count; }
uint64_t test_star_key(int i) { return i>=0 && i<star_count ? star_keys[i] : 0; }
void test_current_key(uint64_t key) { state.track_key=key; }
int test_starred(void) { return state.starred; }
int test_stars_pending(void) { return stars_refresh_pending; }
unsigned test_star_revision(void) { return star_revision; }
const char *test_playlist_name(void) { return playlist_name; }
void test_close(void) { mpd_wire_close(&wire); }
uint64_t test_insert(const char *uri) { return uri_insert(uri) ? mpd_id(uri) : 0; }
int test_parse(int mode) {
    song_parser_t p={.library=mode==0,.queue=mode==1,.stored=mode==2,.playlist=0};
    uint64_t keys[QUEUE_LIMIT]; p.keys=keys; p.ids=parsed_queue_ids;
    bool ok=mpd_wire_command(&wire,"test_metadata",song_field,NULL,&p) && song_finish(&p);
    return ok ? p.count : -1;
}
int test_sync(void) { return sync_library(); }
int test_run(int kind,uint64_t *keys,int count,int start) {
    command_t c={.kind=kind,.keys=keys,.count=count,.start=start,.queue_id=(uint32_t)start}; return run_command(&c);
}
int test_queue_add(uint64_t *keys,int count,int next) { return queue_add_selection(keys,count,next); }
int test_queue_edit(uint32_t id,int remove,int epoch) { return queue_edit_entry(((uint64_t)epoch<<32)|id,remove); }
int test_priority_tick(uint32_t id,int valid) { return queue_next_tick(id,valid,true); }
void test_queue_removed_head(void) { queue_size=0;queue_next_queue_changed(); }
int test_run_shuffled(uint64_t *keys,int count) {
    command_t c={.kind=CMD_PLAY,.keys=keys,.count=count,.shuffle=true};return run_command(&c);
}
int test_coloplay(int kind,int usage,int count,int parameter,int level,int play,uint32_t session) {
    command_t c={.kind=CMD_COLOPLAY,.remote_result=CP_APPLIED};
    cp_header(c.coloplay,kind,1,session);cp_put16(c.coloplay+8,usage);cp_put16(c.coloplay+10,count);cp_put16(c.coloplay+12,parameter);
    server_volume=level;state.volume=level<0?-1.0f:level/100.0f;state.play_status=play;coloplay_session=7;
    if(!run_command(&c)){return CP_FAILED;}return c.remote_result;
}
int test_coloplay_retired_epoch(void) {
    ++coloplay_epoch;int result=test_coloplay(2,233,1,1,50,1,7);--coloplay_epoch;return result;
}
float test_fraction(void) { return state.volume; }
float test_grid_target(void) { return state.volume_grid_target; }
int test_grid_steps(void) { return state.volume_grid_steps; }
int test_volume_request(void) { return state.volume_request; }
int test_remote_confirm(int id,int direction) {
    command_t c={.kind=CMD_COLOPLAY,.remote_result=CP_APPLIED};
    coloplay_session=7;cp_header(c.coloplay,CP_VOLUME,id,7);
    cp_put16(c.coloplay+8,direction>0?233:234);cp_put16(c.coloplay+10,1);cp_put16(c.coloplay+12,1);
    if(!run_command(&c))return 0;
    coloplay_volume_complete(&c);return poll_state();
}
int test_poll(void) { return poll_state(); }
int test_step_confirm(int direction) {
    command_t c={.kind=CMD_TEXT};snprintf(c.text,sizeof(c.text),"@volume_%s",direction>0?"up":"down");
    return run_command(&c) && poll_state();
}
int test_coloplay_seeking(void) { return coloplay_seek; }
void test_set_volume_steps(int n) { volume_steps=n; }
int test_volume_mode_change(void) {
    server_volume=67;state.volume=0.67f;volume_steps=64;
    command_t c={.kind=CMD_TEXT};strcpy(c.text,"@volume_up");return run_command(&c);
}
int test_toggle(const char *name,int repeat,int volume,int shuffle) {
    state.repeat=repeat; server_volume=volume;state.volume=volume<0?-1.0f:volume/100.0f; state.shuffle=shuffle;
    command_t c={.kind=CMD_TEXT}; snprintf(c.text,sizeof(c.text),"%s",name);
    return run_command(&c);
}
uint64_t test_track_key(int i) { return items[i].pid; }
uint64_t test_album_key(int i) { return items[i].album_pid; }
const char *test_title(int i) { return items[i].title; }
const char *test_artist(int i) { return items[i].artist; }
unsigned test_duration(int i) { return items[i].duration_ms; }
unsigned test_year(int i) { return items[i].year; }
unsigned test_date(int i) { return items[i].date_added; }
unsigned test_queue_entry_id(int i) { return parsed_queue_ids[i]; }
int test_item_count(void) { return item_count; }
int test_playlist_count(void) { return playlist_count; }
const char *test_message(void) { return diagnostic; }
const char *test_wire_error(void) { return wire.error; }
'''

class ClientTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory()
        directory=Path(cls.tmp.name)
        (directory/'esp_err.h').write_text('typedef int esp_err_t;\n')
        uri_type=re.search(r'typedef struct \{ uint64_t id; uint32_t offset; \} uri_entry_t;',SOURCE).group()
        start=SOURCE.index('typedef enum { CMD_TEXT')
        command_type=SOURCE[start:SOURCE.index('} command_t;',start)+len('} command_t;')]
        start=SOURCE.index('typedef struct {\n    player_media_item_t item;')
        song_type=SOURCE[start:SOURCE.index('} song_parser_t;',start)+len('} song_parser_t;')]
        globals_='typedef struct { char names[32][256]; int count; } playlists_t;\n'+uri_type+'''
static playlists_t *playlist_destinations;
static uint32_t playlist_destination_generation,playlist_connection_generation=1,config_revision=1;
static bool playlists_refresh_pending;static void *mutex=(void*)1;
typedef struct {const char *name;bool found;} playlist_search_t;
typedef struct {const uint64_t *keys;int count,seen;bool changed;} playlist_check_t;
static uri_entry_t uris[TRACK_LIMIT];
static uint16_t uri_slots[URI_SLOTS];
static char uri_pool[URI_BYTES];
static unsigned uri_count,uri_used;
'''
        queue_types=re.search(r'typedef struct \{ int position; uint32_t id; bool have_id; \} queue_entry_info_t;',SOURCE).group()+'\n'+re.search(r'typedef struct \{ uint32_t \*ids;int count,capacity; \} queue_added_t;',SOURCE).group()+'\n'
        public_source=re.sub(r'^((?:int|bool|uint64_t) mpd_client_[^(]+\()',r'static \1',SOURCE,flags=re.M)
        def production(name):
            if name.startswith('mpd_client_'):return function(public_source,name).removeprefix('static ')
            return function(SOURCE,name)
        functions='\n'.join(production(name) for name in [
            'publish','uri_contains','uri_lookup','uri_insert','song_reset','song_finish','song_field',
            'status_field','stat_field','simple','sync_library','queue_request_locked','queue_page_pending','queue_reset_locked','sync_queue','star_position',
            'sync_stars','change_star','playlist_field','sync_playlists','mpd_client_playlist_destination_count','mpd_client_playlist_destination','playlist_search_field','playlist_add_track','playlist_check_field','playlist_remove_track','play_selection','poll_state','queue_page_field','load_queue_page','mpd_client_queue_list_get','mpd_client_queue_entry','mpd_client_queue_loading','set_volume','queue_next_queue_changed','queue_priority_field','queue_priority_capability','queue_next_tick','queue_next_install','queue_added_field','queue_entry_field','queue_add_selection','queue_edit_entry','run_command','coloplay_volume_complete'])
        (directory/'client.c').write_text(PREFIX+globals_+command_type+song_type+queue_types+functions+SUFFIX)
        subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Wno-unused-function','-shared','-fPIC',
                        '-I'+str(directory),'-I'+str(ROOT/'app/mpd'),'-I'+str(ROOT/'app/backend'),
                        '-I'+str(ROOT/'firmware/main'),str(directory/'client.c'),
                        str(ROOT/'app/mpd/mpd_wire.c'),'-o',str(directory/'client.so')],check=True)
        cls.lib=C.CDLL(str(directory/'client.so'))
        cls.lib.mpd_client_queue_list_get.argtypes=[C.c_int,C.POINTER(C.c_uint64)]
        cls.lib.mpd_client_queue_entry.restype=C.c_uint64
        cls.lib.test_seed.argtypes=[C.c_int,C.c_uint64,C.c_uint]
        cls.lib.test_playlist_remove.argtypes=[C.c_char_p,C.POINTER(C.c_uint64),C.c_int,C.c_int,C.c_uint,C.c_uint]
        cls.lib.test_playlist_add.argtypes=[C.c_char_p,C.c_uint64,C.c_uint,C.c_uint]
        cls.lib.test_destination_name.argtypes=[C.c_int];cls.lib.test_destination_name.restype=C.c_char_p
        cls.lib.test_fraction.restype=C.c_float
        cls.lib.test_grid_target.restype=C.c_float
        cls.lib.test_insert.argtypes=[C.c_char_p]; cls.lib.test_insert.restype=C.c_uint64
        cls.lib.test_run.argtypes=[C.c_int,C.POINTER(C.c_uint64),C.c_int,C.c_int]
        cls.lib.test_run_shuffled.argtypes=[C.POINTER(C.c_uint64),C.c_int]
        cls.lib.test_current_key.argtypes=[C.c_uint64]
        cls.lib.test_star_key.restype=C.c_uint64
        cls.lib.test_toggle.argtypes=[C.c_char_p,C.c_int,C.c_int,C.c_int]
        cls.lib.test_coloplay.argtypes=[C.c_int,C.c_int,C.c_int,C.c_int,C.c_int,C.c_int,C.c_uint32]
        for name in ['test_track_key','test_album_key']:
            getattr(cls.lib,name).restype=C.c_uint64
        for name in ['test_title','test_artist','test_message','test_playlist_name','test_wire_error']:
            getattr(cls.lib,name).restype=C.c_char_p

    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def setUp(self):
        self.peer,client=socket.socketpair(); self.peer.settimeout(2)
        self.peer.sendall(b'OK MPD 0.24.0\n'); self.lib.test_init(client.detach())
        self.threads=[]; self.errors=[]
    def tearDown(self):
        self.lib.test_close(); self.peer.close()
        for t in self.threads:
            t.join(3); self.assertFalse(t.is_alive())
        if self.errors: raise self.errors[0]
    def server(self, exchanges):
        def run():
            try:
                f=self.peer.makefile('rb')
                for expected,response in exchanges:
                    actual=b''
                    for _ in range(expected.count(b'\n')): actual+=f.readline()
                    self.assertEqual(actual,expected)
                    self.peer.sendall(response)
            except Exception as error: self.errors.append(error)
        t=threading.Thread(target=run); self.threads.append(t); t.start()
    def metadata(self,data,mode=0):
        self.server([(b'test_metadata\n',data+b'OK\n')])
        return self.lib.test_parse(mode)

    def star_reply(self,*uris):
        return b''.join(b'file: '+uri+b'\nTitle: Song\nAlbum: Album\nArtist: Artist\n' for uri in uris)+b'OK\n'

    def test_add_to_playlist_quotes_name_and_uri_without_playback_commands(self):
        uri=b'album/a "quoted" \\ song.flac';name=b'Evening "mix" \\ 2026'
        key=self.lib.test_insert(uri)
        def quote(value):return b'"'+value.replace(b'\x5c',b'\x5c\x5c').replace(b'\x22',b'\x5c\x22')+b'"'
        self.server([(b'listplaylists\n',b'playlist: '+name+b'\nOK\n'),
                     (b'playlistadd '+quote(name)+b' '+quote(uri)+b'\n',b'OK\n')])
        self.assertEqual(self.lib.test_playlist_add(name,key,1,1),1)
        self.assertEqual(self.lib.test_playlists_pending(),1)
        self.assertEqual(self.lib.test_queue_pending(),0)

    def test_deleted_destination_is_not_recreated_or_redirected(self):
        key=self.lib.test_insert(b'album/song.flac')
        self.server([(b'listplaylists\n',b'playlist: Another list\nOK\n')])
        self.assertEqual(self.lib.test_playlist_add(b'Deleted list',key,1,1),1)
        self.assertIn(b'Playlist changed',self.lib.test_message())
        self.assertEqual(self.lib.test_playlists_pending(),1)

    def test_playlist_connection_change_rejects_old_picker_without_io(self):
        key=self.lib.test_insert(b'album/song.flac')
        self.assertEqual(self.lib.test_playlist_add(b'Mix',key,1,2),1)
        self.assertIn(b'connection changed',self.lib.test_message())
        self.peer.settimeout(.05)
        with self.assertRaises(socket.timeout):self.peer.recv(1)

    def test_playlist_add_rejects_reserved_name_missing_track_and_line_injection(self):
        key=self.lib.test_insert(b'album/song.flac')
        self.assertEqual(self.lib.test_playlist_add(b'sTaReD',key,1,1),1)
        self.assertEqual(self.lib.test_playlist_add(b'Mix',key+1,1,1),1)
        self.assertEqual(self.lib.test_playlist_add(b'Bad\nclear',key,1,1),0)
        self.peer.settimeout(.05)
        with self.assertRaises(socket.timeout):self.peer.recv(1)

    def test_playlist_write_error_keeps_socket_and_does_not_claim_success(self):
        key=self.lib.test_insert(b'album/song.flac')
        self.server([(b'listplaylists\n',b'playlist: Mix\nOK\n'),
                     (b'playlistadd "Mix" "album/song.flac"\n',b'ACK [4@0] {playlistadd} permission denied\n')])
        self.assertEqual(self.lib.test_playlist_add(b'Mix',key,1,1),0)
        self.assertEqual(self.lib.test_playlists_pending(),0)
        self.assertIn(b'permission denied',self.lib.test_wire_error())

    def test_playlist_destinations_keep_full_names_and_connection_identity(self):
        name=b'A'*180+b' mix'
        self.server([(b'listplaylists\n',b'playlist: Stared\nplaylist: '+name+b'\nOK\n'),
                     (b'listplaylistinfo "'+name+b'"\n',b'OK\n'),
                     (b'listplaylistinfo "Stared"\n',b'ACK [50@0] {listplaylistinfo} missing\n')])
        self.assertEqual(self.lib.test_sync_playlists(),1)
        self.assertEqual(self.lib.test_destination_count(1,1),1)
        self.assertEqual(self.lib.test_destination_name(0),name)
        self.assertEqual(self.lib.test_destination_count(2,1),0)
        self.assertIsNone(self.lib.test_destination_name(0))
        self.assertEqual(self.lib.test_destination_count(1,0),0)

    def test_remove_playlist_targets_one_duplicate_position_with_quoted_full_name(self):
        name=b'A'*180+b' "mix" \\ night';uris=[b'a.flac',b'b.flac',b'a.flac']
        keys=(C.c_uint64*3)(*[self.lib.test_insert(uri) for uri in uris])
        quoted=b'"'+name.replace(b'\\',b'\\\\').replace(b'"',b'\\"')+b'"'
        self.server([(b'listplaylistinfo '+quoted+b'\n',self.star_reply(*uris)),
                     (b'playlistdelete '+quoted+b' 2\n',b'OK\n')])
        self.assertEqual(self.lib.test_playlist_remove(name,keys,3,2,1,1),1)
        self.assertEqual(self.lib.test_playlists_pending(),1)
        self.assertEqual(self.lib.test_queue_pending(),0)

    def test_remove_playlist_rechecks_entire_visible_order_even_if_selected_key_matches(self):
        a=self.lib.test_insert(b'a.flac');b=self.lib.test_insert(b'b.flac')
        keys=(C.c_uint64*3)(a,b,a)
        self.server([(b'listplaylistinfo "Mix"\n',self.star_reply(b'b.flac',b'a.flac',b'a.flac'))])
        self.assertEqual(self.lib.test_playlist_remove(b'Mix',keys,3,2,1,1),1)
        self.assertIn(b'Playlist changed',self.lib.test_message())
        self.assertEqual(self.lib.test_playlists_pending(),1)

    def test_remove_playlist_rejects_shortened_or_extended_complete_snapshot(self):
        key=self.lib.test_insert(b'a.flac');keys=(C.c_uint64*2)(key,key)
        self.server([(b'listplaylistinfo "Mix"\n',self.star_reply(b'a.flac')),
                     (b'listplaylistinfo "Mix"\n',self.star_reply(b'a.flac',b'a.flac',b'a.flac'))])
        self.assertEqual(self.lib.test_playlist_remove(b'Mix',keys,2,0,1,1),1)
        self.assertEqual(self.lib.test_playlist_remove(b'Mix',keys,2,0,1,1),1)
        self.assertIn(b'Playlist changed',self.lib.test_message())

    def test_remove_playlist_supports_visible_prefix_of_large_playlist(self):
        key=self.lib.test_insert(b'a.flac');keys=(C.c_uint64*1024)(*([key]*1024))
        self.server([(b'listplaylistinfo "Mix"\n',self.star_reply(*([b'a.flac']*1025))),
                     (b'playlistdelete "Mix" 1023\n',b'OK\n')])
        self.assertEqual(self.lib.test_playlist_remove(b'Mix',keys,1024,1023,1,1),1)

    def test_remove_playlist_stale_connection_is_rejected_without_io(self):
        key=self.lib.test_insert(b'a.flac');keys=(C.c_uint64*1)(key)
        self.assertEqual(self.lib.test_playlist_remove(b'Mix',keys,1,0,1,2),1)
        self.assertIn(b'connection changed',self.lib.test_message())
        self.peer.settimeout(.05)
        with self.assertRaises(socket.timeout):self.peer.recv(1)

    def test_remove_playlist_server_errors_do_not_report_success_or_touch_queue(self):
        key=self.lib.test_insert(b'a.flac');keys=(C.c_uint64*1)(key)
        self.server([(b'listplaylistinfo "Deleted"\n',b'ACK [50@0] {listplaylistinfo} missing\n'),
                     (b'listplaylistinfo "Mix"\n',self.star_reply(b'a.flac')),
                     (b'playlistdelete "Mix" 0\n',b'ACK [4@0] {playlistdelete} permission denied\n')])
        self.assertEqual(self.lib.test_playlist_remove(b'Deleted',keys,1,0,1,1),0)
        self.assertEqual(self.lib.test_playlist_remove(b'Mix',keys,1,0,1,1),0)
        self.assertEqual(self.lib.test_playlists_pending(),0)
        self.assertEqual(self.lib.test_queue_pending(),0)
        self.assertIn(b'permission denied',self.lib.test_wire_error())

    def test_elapsed_polls_preserve_revision_but_publish_real_changes(self):
        revision=self.lib.test_publish(1000,50,0,b"Song")
        self.assertEqual(self.lib.test_publish(2000,50,0,b"Song"),revision)
        self.assertEqual(self.lib.test_elapsed(),2000)
        self.assertEqual(self.lib.test_publish(2000,51,0,b"Song"),revision+1)
        self.assertEqual(self.lib.test_publish(2000,51,1,b"Song"),revision+2)
        self.assertEqual(self.lib.test_publish(2000,51,1,b"Renamed"),revision+3)

    def test_server_stars_are_read_in_playlist_order_and_publish_current_flag(self):
        a=self.lib.test_insert(b'album/a.flac');b=self.lib.test_insert(b'album/b.flac')
        self.lib.test_current_key(a)
        self.server([(b'listplaylistinfo "Stared"\n',self.star_reply(b'album/b.flac',b'album/a.flac'))])
        self.assertEqual(self.lib.test_sync_stars(100000),1)
        self.assertEqual([self.lib.test_star_key(i) for i in range(2)],[b,a])
        self.assertEqual(self.lib.test_starred(),1)
        self.assertEqual(self.lib.test_completions(),0)

    def test_star_add_is_confirmed_by_rereading_server(self):
        key=self.lib.test_insert(b'album/a.flac');self.lib.test_current_key(key)
        self.server([(b'listplaylistinfo "Stared"\n',b'OK\n'),
                     (b'playlistadd "Stared" "album/a.flac"\n',b'OK\n'),
                     (b'listplaylistinfo "Stared"\n',self.star_reply(b'album/a.flac'))])
        keys=(C.c_uint64*1)(key)
        self.assertEqual(self.lib.test_run(4,keys,1,0),1)
        self.assertEqual(self.lib.test_starred(),1)
        self.assertEqual(self.lib.test_completions(),0)

    def test_star_delete_uses_fresh_server_position_and_captured_track(self):
        a=self.lib.test_insert(b'album/a.flac');b=self.lib.test_insert(b'album/b.flac')
        self.lib.test_current_key(b)
        self.server([(b'listplaylistinfo "Stared"\n',self.star_reply(b'album/b.flac',b'album/a.flac')),
                     (b'playlistdelete "Stared" 1\n',b'OK\n'),
                     (b'listplaylistinfo "Stared"\n',self.star_reply(b'album/b.flac'))])
        self.assertEqual(self.lib.test_run(4,(C.c_uint64*1)(a),1,0),1)
        self.assertEqual(self.lib.test_starred(),1)
        self.assertEqual(self.lib.test_star_count(),1)
        self.assertEqual(self.lib.test_star_key(0),b)

    def test_failed_star_write_does_not_change_confirmed_state_or_close_socket(self):
        key=self.lib.test_insert(b'album/a.flac');self.lib.test_current_key(key)
        self.server([(b'listplaylistinfo "Stared"\n',b'OK\n'),
                     (b'playlistadd "Stared" "album/a.flac"\n',b'ACK [4@0] {playlistadd} permission denied\n'),
                     (b'test_metadata\n',b'OK\n')])
        self.assertEqual(self.lib.test_run(4,(C.c_uint64*1)(key),1,0),0)
        self.assertEqual(self.lib.test_starred(),0)
        self.assertEqual(self.lib.test_parse(0),0)

    def test_star_collection_allocation_failure_drains_and_preserves_previous_state(self):
        key=self.lib.test_insert(b'album/a.flac');self.lib.test_current_key(key)
        self.server([(b'listplaylistinfo "Stared"\n',self.star_reply(b'album/a.flac')),
                     (b'listplaylistinfo "Stared"\n',self.star_reply(b'album/a.flac')),
                     (b'test_metadata\n',b'OK\n')])
        self.assertEqual(self.lib.test_sync_stars(100000),1)
        self.assertEqual(self.lib.test_sync_stars(1),1)
        self.assertEqual(self.lib.test_stars_pending(),1)
        self.assertEqual(self.lib.test_star_count(),1)
        self.assertEqual(self.lib.test_starred(),1)
        self.assertEqual(self.lib.test_parse(0),0)

    def test_denied_star_read_does_not_send_a_write(self):
        key=self.lib.test_insert(b'album/a.flac')
        self.server([(b'listplaylistinfo "Stared"\n',b'ACK [4@0] {listplaylistinfo} permission denied\n')])
        self.assertEqual(self.lib.test_run(4,(C.c_uint64*1)(key),1,0),1)
        self.assertEqual(self.lib.test_stars_pending(),1)
        self.assertEqual(self.lib.test_starred(),0)

    def test_clear_stars_updates_server_playlist_only(self):
        self.server([(b'listplaylistinfo "Stared"\n',self.star_reply(b'album/a.flac')),
                     (b'playlistclear "Stared"\n',b'OK\n'),
                     (b'listplaylistinfo "Stared"\n',b'OK\n')])
        self.assertEqual(self.lib.test_run(5,None,0,0),1)
        self.assertEqual(self.lib.test_star_count(),0)

    def test_missing_star_playlist_is_empty_server_state(self):
        self.server([(b'listplaylistinfo "Stared"\n',b'ACK [50@0] {listplaylistinfo} No such playlist\n')])
        self.assertEqual(self.lib.test_sync_stars(100000),1)
        self.assertEqual(self.lib.test_star_count(),0)
        self.assertEqual(self.lib.test_stars_pending(),0)

    def test_star_playlist_exceeds_normal_stored_playlist_limit(self):
        uris=[f'album/{i}.flac'.encode() for i in range(1200)]
        self.server([(b'listplaylistinfo "Stared"\n',self.star_reply(*uris))])
        self.assertEqual(self.lib.test_sync_stars(100000),1)
        self.assertEqual(self.lib.test_star_count(),1200)
        self.assertEqual(self.lib.test_playlist_count(),0)
        self.assertTrue(self.lib.test_star_key(1199))

    def test_stared_is_not_duplicated_in_regular_playlists_or_catalog_updates(self):
        self.lib.test_insert(b'album/a.flac')
        self.server([(b'listplaylists\n',b'playlist: Mix\nplaylist: Stared\nOK\n'),
                     (b'listplaylistinfo "Mix"\n',self.star_reply(b'album/a.flac')),
                     (b'listplaylistinfo "Stared"\n',self.star_reply(b'album/a.flac'))])
        self.assertEqual(self.lib.test_sync_playlists(),1)
        self.assertEqual(self.lib.test_playlist_name(),b'Mix')
        self.assertEqual(self.lib.test_playlist_count(),2)
        self.assertEqual(self.lib.test_star_count(),1)
        self.assertEqual(self.lib.test_completions(),0)

    def queue_page_reply(self,start,end,total,version=1,paths=None):
        paths=paths or [f'album/{i}.flac'.encode() for i in range(start,end)]
        data=b''.join(b'file: '+path+b'\nId: '+str(i).encode()+b'\n' for i,path in zip(range(start,end),paths))
        return [(f'playlistinfo {start}:{end}\n'.encode(),data+b'OK\n'),
                (b'status\n',f'state: pause\nplaylist: {version}\nplaylistlength: {total}\nOK\n'.encode()),
                (b'currentsong\n',b'OK\n')]

    def queue_get(self,index):
        key=C.c_uint64()
        return bool(self.lib.mpd_client_queue_list_get(index,C.byref(key))),key.value

    def test_large_queue_startup_only_publishes_count_without_network_or_allocation(self):
        self.assertEqual(self.lib.test_sync_queue(20916,0),1)
        self.assertEqual(self.lib.test_queue_count(),20916)
        self.assertEqual(self.lib.test_cached(),0)
        self.assertEqual(self.lib.test_requests(),0)
        self.assertEqual(self.lib.test_allocation(),0)
        self.peer.settimeout(.05)
        with self.assertRaises(socket.timeout):self.peer.recv(1)

    def test_current_song_seed_is_available_without_fetching_its_page(self):
        key=self.lib.test_insert(b'album/current.flac')
        self.lib.test_seed(12000,key,0)
        self.assertEqual(self.lib.test_sync_queue(20916,0),1)
        self.assertEqual(self.queue_get(12000),(True,key))
        self.assertEqual(self.lib.test_cached(),1)
        self.assertEqual(self.lib.test_requests(),0)
        self.assertEqual(self.lib.mpd_client_queue_entry(12000),1<<32) # ID zero is valid
        self.assertEqual(self.lib.mpd_client_queue_entry(11999),0)

    def test_requested_page_is_bounded_and_cache_hits_do_not_request_again(self):
        self.lib.test_sync_queue(20916,0)
        for i in range(64):self.lib.test_insert(f'album/{i}.flac'.encode())
        self.assertFalse(self.queue_get(0)[0]);self.assertFalse(self.queue_get(63)[0])
        self.assertEqual(self.lib.test_requests(),1)
        revision=self.lib.test_queue_revision()
        self.server(self.queue_page_reply(0,64,20916))
        self.assertEqual(self.lib.test_page(),1)
        self.assertEqual(self.lib.test_queue_revision(),revision)
        self.assertEqual(self.lib.test_cached(),64)
        self.assertEqual(self.lib.test_requests(),0)
        for i in range(64):self.assertTrue(self.queue_get(i)[0])
        self.assertEqual(self.lib.test_requests(),0)
        self.assertEqual(self.lib.test_completions(),0)
        self.assertEqual(self.lib.test_allocation(),0)
        self.assertEqual(self.lib.test_queue_count(),20916)

    def test_far_page_and_short_last_page_load_without_intermediate_rows(self):
        self.lib.test_sync_queue(20916,0)
        self.assertFalse(self.queue_get(20915)[0])
        self.server(self.queue_page_reply(20864,20916,20916))
        self.assertEqual(self.lib.test_page(),1)
        self.assertEqual(self.lib.test_cached(),52)
        self.assertTrue(self.queue_get(20915)[0])
        self.assertEqual(self.lib.test_published_queue_id(20915),20915)
        self.assertEqual(self.lib.mpd_client_queue_entry(0),0)

    def test_new_queue_only_tracks_publish_catalog_once(self):
        self.lib.test_sync_queue(1,1024);self.queue_get(0)
        self.server(self.queue_page_reply(0,1,1,paths=[b'stream/new']))
        self.assertEqual(self.lib.test_page(),1)
        self.assertEqual(self.lib.test_completions(),1)
        self.assertEqual(self.lib.test_item_count(),1)
        self.lib.test_sync_queue(1,1024);self.queue_get(0)
        self.server(self.queue_page_reply(0,1,1,paths=[b'stream/new']))
        self.assertEqual(self.lib.test_page(),1)
        self.assertEqual(self.lib.test_completions(),1)

    def test_changed_queue_version_discards_inflight_page_then_retries(self):
        self.lib.test_sync_queue(64,1024);self.queue_get(0)
        self.server(self.queue_page_reply(0,64,64,version=2))
        self.assertEqual(self.lib.test_page(),1)
        self.assertEqual(self.lib.test_cached(),0)
        self.assertEqual(self.lib.test_queue_pending(),1)
        self.lib.test_sync_queue(64,1024);self.queue_get(0)
        self.server(self.queue_page_reply(0,64,64,version=2))
        self.assertEqual(self.lib.test_page(),1)
        self.assertEqual(self.lib.test_cached(),64)

    def test_queue_growth_without_version_change_discards_old_page(self):
        self.lib.test_sync_queue(10,1024);self.queue_get(0)
        self.server(self.queue_page_reply(0,10,11))
        self.assertEqual(self.lib.test_page(),1)
        self.assertEqual(self.lib.test_cached(),0)
        self.assertEqual(self.lib.test_queue_pending(),1)

    def test_range_failure_does_not_fall_back_to_full_queue(self):
        self.lib.test_sync_queue(20916,0);self.queue_get(12345)
        self.server([(b'playlistinfo 12288:12352\n',b'ACK [2@0] {playlistinfo} unsupported range\n')])
        self.assertEqual(self.lib.test_page(),1)
        self.assertEqual(self.lib.test_cached(),0)
        self.assertFalse(self.lib.mpd_client_queue_loading(12345))
        self.assertFalse(self.queue_get(12345)[0])
        self.assertEqual(self.lib.test_requests(),0)
        self.peer.settimeout(.05)
        with self.assertRaises(socket.timeout):self.peer.recv(1)

    def test_server_ignoring_range_is_stopped_without_reading_full_queue(self):
        self.lib.test_sync_queue(20916,0);self.queue_get(0)
        data=b''.join(f'file: album/{i}.flac\nId: {i}\n'.encode() for i in range(70))
        self.server([(b'playlistinfo 0:64\n',data+b'OK\n')])
        self.assertEqual(self.lib.test_page(),0) # malformed range closes the wire
        self.assertEqual(self.lib.test_cached(),0)
        self.assertEqual(self.lib.test_allocation(),0)
        self.assertLessEqual(self.lib.test_item_count(),65)

    def test_out_of_range_rows_never_request_pages(self):
        self.lib.test_sync_queue(64,0)
        self.assertFalse(self.queue_get(-1)[0]);self.assertFalse(self.queue_get(64)[0])
        self.assertEqual(self.lib.test_requests(),0)

    def test_database_import_is_paginated_and_flushes_each_page(self):
        first=b''.join(f'file: music/{i}.flac\nTitle: Song {i}\n'.encode() for i in range(256))
        self.server([
            (b'stats\n',b'songs: 257\ndb_update: 123\nOK\n'),
            (b'find "(base \'\')" window 0:256\n',first+b'OK\n'),
            (b'find "(base \'\')" window 256:512\n',b'file: music/256.flac\nTitle: Last\nOK\n')])
        self.assertEqual(self.lib.test_sync(),1)
        self.assertEqual(self.lib.test_item_count(),257)
        self.assertEqual(self.lib.test_title(256),b'Last')

    def test_tracks_empty_find_falls_back_to_complete_library(self):
        self.server([
            (b'stats\n',b'songs: 2\ndb_update: 1794555168\nOK\n'),
            (b'find "(base \'\')" window 0:256\n',b'OK\n'),
            (b'listallinfo\n',b'file: /Music/a.flac\nTitle: Alpha\nadded: 2024-01-02T03:04:05Z\nfile: /Music/b.flac\nTitle: Beta\nadded: 2025-01-01T00:00:00Z\nOK\n')])
        self.assertEqual(self.lib.test_sync(),1)
        self.assertEqual(self.lib.test_item_count(),2)
        self.assertEqual(self.lib.test_title(1),b'Beta')
        self.assertEqual(self.lib.test_date(0),1704164645)

    def test_long_unicode_title_is_clipped_at_a_complete_character(self):
        title=('a'*94+'🐈').encode()
        self.assertEqual(self.metadata(b'file: /Music/a.flac\nTitle: '+title+b'\n'),1)
        self.assertEqual(self.lib.test_title(0).decode(),'a'*94)

    def test_added_date_takes_priority_over_last_modified(self):
        self.assertEqual(self.metadata(b'file: /Music/a.flac\nLast-Modified: 2025-01-01T00:00:00Z\nadded: 2024-01-02T03:04:05Z\n'),1)
        self.assertEqual(self.lib.test_date(0),1704164645)

    def test_unknown_added_date_falls_back_to_modified(self):
        self.assertEqual(self.metadata(b'file: /Music/a.flac\nadded: -1\nLast-Modified: 2024-01-02T03:04:05Z\n'),1)
        self.assertEqual(self.lib.test_date(0),1704164645)

    def test_duplicate_queue_tracks_keep_distinct_server_entry_ids(self):
        self.assertEqual(self.metadata(b'file: /Music/a.flac\nTitle: Alpha\nId: 17\nfile: /Music/a.flac\nTitle: Alpha\nId: 99\n',1),2)
        self.assertEqual([self.lib.test_queue_entry_id(i) for i in range(2)],[17,99])

    def test_queue_selection_longer_than_512_is_sent_in_full(self):
        paths=[f'Music/{i}.flac'.encode() for i in range(700)]
        keys=(C.c_uint64*len(paths))(*[self.lib.test_insert(p) for p in paths])
        exchanges=[(b'command_list_begin\nclear\nrandom 0\nsingle 0\ncommand_list_end\n',b'OK\n')]
        for start in range(0,len(paths),100):
            commands=b'command_list_begin\n'+b''.join(b'add "'+p+b'"\n' for p in paths[start:start+100])+b'command_list_end\n'
            exchanges.append((commands,b'OK\n'))
        exchanges.append((b'play 0\n',b'OK\n'));self.server(exchanges)
        self.assertEqual(self.lib.test_run(1,keys,len(paths),0),1)

    def test_whole_library_shuffle_uses_one_server_request_without_key_allocations(self):
        for i in range(20916):self.lib.test_insert(f'library/song-{i}.flac'.encode())
        self.server([(b'command_list_begin\nclear\nrandom 0\nsingle 0\nadd "/"\nshuffle\nrandom 1\nplay 0\ncommand_list_end\n',b'OK\n')])
        self.assertEqual(self.lib.test_shuffle_all(),1)
        self.assertEqual(self.lib.test_allocation(),0)

    def test_shuffle_invalidates_old_entries_without_fetching_large_queue(self):
        key=self.lib.test_insert(b'library/song.flac')
        self.lib.test_seed(0,key,0);self.lib.test_sync_queue(1,0)
        entry=self.lib.mpd_client_queue_entry(0)
        self.server([(b'command_list_begin\nclear\nrandom 0\nsingle 0\nadd "/"\nshuffle\nrandom 1\nplay 0\ncommand_list_end\n',b'OK\n')])
        self.assertEqual(self.lib.test_shuffle_all(),1)
        self.assertEqual(self.lib.test_queue_count(),0)
        self.assertEqual(self.lib.mpd_client_queue_entry(0),0)
        self.lib.test_seed(-1,0,0)
        self.assertEqual(self.lib.test_sync_queue(20916,0),1)
        self.assertEqual(self.lib.test_queue_count(),20916)
        self.assertEqual(self.lib.test_allocation(),0)
        self.assertEqual(self.lib.test_cached(),0)
        self.assertEqual(self.lib.test_requests(),0)
        self.assertEqual(self.lib.test_queue_edit(entry&0xffffffff,1,entry>>32),1)
        self.assertIn(b'Queue connection changed',self.lib.test_message())

    def test_whole_library_shuffle_does_not_clear_for_empty_catalog(self):
        self.assertEqual(self.lib.test_shuffle_all(),1)
        self.assertIn(b'Library is empty',self.lib.test_message())
        self.peer.settimeout(.05)
        with self.assertRaises(socket.timeout):self.peer.recv(1)

    def test_whole_library_shuffle_preserves_server_errors_without_fallback_play(self):
        self.lib.test_insert(b'library/song.flac')
        self.server([(b'command_list_begin\nclear\nrandom 0\nsingle 0\nadd "/"\nshuffle\nrandom 1\nplay 0\ncommand_list_end\n',b'ACK [4@3] {add} permission denied\n')])
        self.assertEqual(self.lib.test_shuffle_all(),0)
        self.assertIn(b'permission denied',self.lib.test_wire_error())

    def test_shuffle_play_shuffles_entire_album_before_first_play(self):
        paths=[f'album/{i}.flac'.encode() for i in range(12)]
        keys=(C.c_uint64*len(paths))(*[self.lib.test_insert(p) for p in paths])
        self.server([
            (b'command_list_begin\nclear\nrandom 0\nsingle 0\ncommand_list_end\n',b'OK\n'),
            (b'command_list_begin\n'+b''.join(b'add "'+p+b'"\n' for p in paths)+b'command_list_end\n',b'OK\n'),
            (b'command_list_begin\nshuffle\nrandom 1\nplay 0\ncommand_list_end\n',b'OK\n')])
        self.assertEqual(self.lib.test_run_shuffled(keys,len(paths)),1)

    def test_failed_shuffle_does_not_start_unshuffled_album(self):
        paths=[b'album/one.flac',b'album/two.flac']
        keys=(C.c_uint64*len(paths))(*[self.lib.test_insert(p) for p in paths])
        self.server([
            (b'command_list_begin\nclear\nrandom 0\nsingle 0\ncommand_list_end\n',b'OK\n'),
            (b'command_list_begin\n'+b''.join(b'add "'+p+b'"\n' for p in paths)+b'command_list_end\n',b'OK\n'),
            (b'command_list_begin\nshuffle\nrandom 1\nplay 0\ncommand_list_end\n',b'ACK [5@0] {shuffle} Failed\n')])
        self.assertEqual(self.lib.test_run_shuffled(keys,len(paths)),0)
        self.threads[-1].join()
        self.peer.setblocking(False)
        with self.assertRaises(BlockingIOError):self.peer.recv(1)

    def test_metadata_uses_uri_identity_and_album_artist(self):
        self.assertEqual(self.metadata(
            b'file: Various/a.flac\nTitle: First\nArtist: Singer A\nAlbum: Mix\nAlbumArtist: Various\nTrack: 1/12\nDisc: 2\nDate: 2024-01-02\nduration: 123.456\n'
            b'file: Various/b.flac\nTitle: Second\nArtist: Singer B\nAlbum: Mix\nAlbumArtist: Various\nduration: 90.5\n'),2)
        self.assertEqual(self.lib.test_title(0),b'First')
        self.assertEqual(self.lib.test_artist(1),b'Singer B')
        self.assertEqual(self.lib.test_duration(0),123456)
        self.assertEqual(self.lib.test_year(0),2024)
        self.assertEqual(self.lib.test_album_key(0),self.lib.test_album_key(1))
        self.assertNotEqual(self.lib.test_track_key(0),self.lib.test_track_key(1))
        self.assertEqual(self.lib.test_track_key(0),self.lib.test_insert(b'Various/a.flac'))

    def test_artist_falls_back_to_album_artist_when_artist_missing_or_blank(self):
        for artist in (b'',b'Artist: \n',b'Artist:  \t \n'):
            self.assertEqual(self.metadata(b'file: fallback.flac\nAlbumArtist: Album Singer\n'+artist),1)
            self.assertEqual(self.lib.test_artist(self.lib.test_item_count()-1),b'Album Singer')

    def test_artist_wins_over_album_artist_in_either_tag_order(self):
        for fields in (b'Artist: Track Singer\nAlbumArtist: Album Singer\n',b'AlbumArtist: Album Singer\nArtist: Track Singer\n'):
            self.assertEqual(self.metadata(b'file: priority.flac\n'+fields),1)
            self.assertEqual(self.lib.test_artist(self.lib.test_item_count()-1),b'Track Singer')

    def test_empty_repeated_artist_tags_do_not_erase_a_real_artist(self):
        self.assertEqual(self.metadata(b'file: repeat.flac\nArtist: Track Singer\nArtist: \nAlbumArtist: Album Singer\nAlbumArtist:  \t\n'),1)
        self.assertEqual(self.lib.test_artist(0),b'Track Singer')

    def test_missing_tags_have_useful_fallbacks_and_final_record_is_flushed(self):
        self.assertEqual(self.metadata(b'file: folder/untagged.flac\n'),1)
        self.assertEqual(self.lib.test_title(0),b'untagged.flac')
        self.assertEqual(self.lib.test_artist(0),b'Unknown artist')
        self.assertNotEqual(self.lib.test_album_key(0),0)

    def test_queue_only_stream_is_registered(self):
        self.assertEqual(self.metadata(b'file: https://radio.example/stream\nName: Radio\nTitle: Live\n',1),1)
        self.assertEqual(self.lib.test_item_count(),1)
        self.assertEqual(self.lib.test_track_key(0),self.lib.test_insert(b'https://radio.example/stream'))

    def test_stored_playlist_imports_streams(self):
        self.assertEqual(self.metadata(b'file: https://radio.example/stream\nTitle: Live\n',2),1)
        self.assertEqual(self.lib.test_item_count(),1)
        self.assertEqual(self.lib.test_playlist_count(),1)

    def test_oversized_uri_fails_explicitly(self):
        self.assertEqual(self.metadata(b'file: '+b'x'*2050+b'\n'),-1)
        self.assertEqual(self.lib.test_item_count(),0)

    def test_queue_replacement_escapes_paths_and_preserves_selection(self):
        paths=[b'Artist/a "quoted".flac',b'Artist/back\\slash.flac']
        keys=(C.c_uint64*2)(*[self.lib.test_insert(p) for p in paths])
        self.server([
            (b'command_list_begin\nclear\nrandom 0\nsingle 0\ncommand_list_end\n',b'OK\n'),
            (b'command_list_begin\nadd "Artist/a \\"quoted\\".flac"\nadd "Artist/back\\\\slash.flac"\ncommand_list_end\n',b'OK\n'),
            (b'play 1\n',b'OK\n')])
        self.assertEqual(self.lib.test_run(1,keys,2,1),1)

    def test_stale_selection_does_not_clear_server_queue(self):
        keys=(C.c_uint64*1)(123)
        self.assertEqual(self.lib.test_run(1,keys,1,0),1)
        self.assertIn(b'stale',self.lib.test_message())
        self.peer.setblocking(False)
        with self.assertRaises(BlockingIOError): self.peer.recv(1)

    def test_queue_jump_plays_captured_server_id(self):
        self.server([(b'playid 3\n',b'OK\n')])
        self.assertEqual(self.lib.test_run(3,None,0,3),1)

    def test_repeat_cycle_matches_ui_off_all_one(self):
        for mode,repeat,single in [(0,1,0),(2,1,1),(1,0,0)]:
            self.server([(f'command_list_begin\nrepeat {repeat}\nsingle {single}\ncommand_list_end\n'.encode(),b'OK\n')])
            self.assertEqual(self.lib.test_toggle(b'@repeat',mode,50,0),1)
            self.threads[-1].join()

    @staticmethod
    def queue_poll(position=1,count=4,random=0):
        song=f'song: {position}\nsongid: 7\n' if position>=0 else ''
        current=b'file: playing.flac\nId: 7\nTitle: Playing\nOK\n' if position>=0 else b'OK\n'
        return [(b'status\n',f'state: pause\nrandom: {random}\nplaylistlength: {count}\n{song}OK\n'.encode()),
                (b'currentsong\n',current)]

    def test_append_preserves_queue_playback_and_selection_order(self):
        paths=[b'album/one.flac',b'album/two.flac']
        keys=(C.c_uint64*2)(*[self.lib.test_insert(p) for p in paths])
        self.server(self.queue_poll(random=1)+[(b'command_list_begin\naddid "album/one.flac" 4\naddid "album/two.flac" 5\ncommand_list_end\n',b'Id: 11\nId: 12\nOK\n')])
        self.assertEqual(self.lib.test_queue_add(keys,2,0),1)
        self.assertTrue(self.lib.test_queue_pending())

    def test_play_next_tracks_shuffle_uses_explicit_insertions(self):
        keys=(C.c_uint64*2)(self.lib.test_insert(b'a.flac'),self.lib.test_insert(b'b.flac'))
        self.server(self.queue_poll(random=1)+[(b'commands\n',b'command: addid\ncommand: moveid\nOK\n'),
            (b'command_list_begin\naddid "a.flac" 2\naddid "b.flac" 3\ncommand_list_end\n',b'Id: 11\nId: 12\nOK\n')])
        self.assertEqual(self.lib.test_queue_add(keys,2,1),1)

    def test_play_next_empty_queue_inserts_at_front_without_play(self):
        keys=(C.c_uint64*1)(self.lib.test_insert(b'a.flac'))
        self.server(self.queue_poll(-1,0)+[(b'command_list_begin\naddid "a.flac" 0\ncommand_list_end\n',b'Id: 0\nOK\n')])
        self.assertEqual(self.lib.test_queue_add(keys,1,1),1)

    def test_play_next_random_prioritizes_each_track_in_order(self):
        keys=(C.c_uint64*2)(self.lib.test_insert(b'a.flac'),self.lib.test_insert(b'b.flac'))
        self.server(self.queue_poll(random=1)+[(b'commands\n',b'command: prioid\nOK\n'),
            (b'command_list_begin\naddid "a.flac" 2\naddid "b.flac" 3\ncommand_list_end\n',b'Id: 0\nId: 12\nOK\n'),
            (b'prioid 255 0\n',b'OK\n'),(b'prioid 255 12\n',b'OK\n')])
        self.assertEqual(self.lib.test_queue_add(keys,2,1),1)
        self.assertEqual(self.lib.test_priority_tick(0,1),1)
        self.assertEqual(self.lib.test_priority_tick(12,1),1)

    def test_new_play_next_precedes_unplayed_previous_request(self):
        a=(C.c_uint64*1)(self.lib.test_insert(b'a.flac'))
        b=(C.c_uint64*1)(self.lib.test_insert(b'b.flac'))
        exchanges=self.queue_poll(random=1)+[(b'commands\n',b'command: prioid\nOK\n'),
            (b'command_list_begin\naddid "a.flac" 2\ncommand_list_end\n',b'Id: 10\nOK\n'),(b'prioid 255 10\n',b'OK\n')]
        exchanges+=self.queue_poll(count=5,random=1)+[(b'command_list_begin\naddid "b.flac" 2\ncommand_list_end\n',b'Id: 11\nOK\n'),
            (b'prioid 0 10\n',b'OK\n'),(b'prioid 255 11\n',b'OK\n'),(b'prioid 255 10\n',b'OK\n')]
        self.server(exchanges)
        self.assertEqual(self.lib.test_queue_add(a,1,1),1)
        self.assertEqual(self.lib.test_queue_add(b,1,1),1)
        self.assertEqual(self.lib.test_priority_tick(11,1),1)

    def test_removed_priority_head_advances_to_remaining_request(self):
        keys=(C.c_uint64*2)(self.lib.test_insert(b'a.flac'),self.lib.test_insert(b'b.flac'))
        self.server(self.queue_poll(random=1)+[(b'commands\n',b'command: prioid\nOK\n'),
            (b'command_list_begin\naddid "a.flac" 2\naddid "b.flac" 3\ncommand_list_end\n',b'Id: 10\nId: 12\nOK\n'),
            (b'prioid 255 10\n',b'OK\n'),(b'prioid 255 10\n',b'ACK [50@0] {prioid} No such song\n'),
            (b'prioid 255 12\n',b'OK\n')])
        self.assertEqual(self.lib.test_queue_add(keys,2,1),1)
        self.assertEqual(self.lib.test_sync_queue(20916,0),1,self.lib.test_wire_error())
        self.assertEqual(self.lib.test_queue_count(),20916)
        self.assertEqual(self.lib.test_cached(),1)
        self.assertEqual(self.lib.test_priority_tick(7,1),1,self.lib.test_wire_error())

    def test_queue_remove_addresses_duplicate_by_entry_id_including_zero(self):
        self.server([(b'deleteid 0\n',b'OK\n')])
        self.assertEqual(self.lib.test_queue_edit(0,1,1),1)

    def test_old_connection_queue_entry_is_rejected_without_io(self):
        self.assertEqual(self.lib.test_queue_edit(99,1,2),1)
        self.assertIn(b'connection changed',self.lib.test_message())

    def test_move_queue_uses_latest_server_position_without_duplicating(self):
        self.server(self.queue_poll()+[(b'playlistid 99\n',b'Id: 99\nPos: 3\nOK\n'),(b'moveid 99 2\n',b'OK\n')])
        self.assertEqual(self.lib.test_queue_edit(99,0,1),1)

    def test_move_entry_before_current_adjusts_destination(self):
        self.server(self.queue_poll(2)+[(b'playlistid 99\n',b'Id: 99\nPos: 0\nOK\n'),(b'moveid 99 2\n',b'OK\n')])
        self.assertEqual(self.lib.test_queue_edit(99,0,1),1)

    def test_move_current_entry_is_noop_after_playback_advances(self):
        self.server(self.queue_poll()+[(b'playlistid 7\n',b'Id: 7\nPos: 1\nOK\n')])
        self.assertEqual(self.lib.test_queue_edit(7,0,1),1)
        self.assertIn(b'already playing',self.lib.test_message())

    def test_stale_library_selection_never_changes_existing_queue(self):
        keys=(C.c_uint64*1)(999)
        self.assertEqual(self.lib.test_queue_add(keys,1,0),1)
        self.assertIn(b'Selection changed',self.lib.test_message())

    def test_large_append_is_batched_without_clearing_or_starting_playback(self):
        paths=[f'Music/{i}.flac'.encode() for i in range(205)]
        keys=(C.c_uint64*len(paths))(*[self.lib.test_insert(p) for p in paths])
        exchanges=self.queue_poll()
        for start in range(0,len(paths),100):
            commands=b'command_list_begin\n'+b''.join(b'addid "'+p+b'" '+str(4+i).encode()+b'\n' for i,p in enumerate(paths[start:start+100],start))+b'command_list_end\n'
            exchanges.append((commands,b'OK\n'))
        self.server(exchanges)
        self.assertEqual(self.lib.test_queue_add(keys,len(paths),0),1)

    @staticmethod
    def volume_poll(level):
        return [(b'status\n',f'volume: {level}\nstate: pause\nOK\n'.encode()),(b'currentsong\n',b'OK\n')]

    @staticmethod
    def target(index,mode):
        count={16:16,64:64,2:50,5:20}[mode]
        return (index/count)**2 if mode in (16,64) else index/count

    @staticmethod
    def percent_text(raw):
        return f'{raw*100:.8f}'.rstrip('0').rstrip('.')

    def test_all_four_scales_round_trip_every_position(self):
        for mode,count in [(16,16),(64,64),(2,50),(5,20)]:
            self.lib.test_set_volume_steps(mode)
            poll=self.volume_poll;current='0';exchanges=poll(0)
            indices=list(range(1,count+1))+list(range(count-1,-1,-1))
            for index in indices:
                level=self.percent_text(self.target(index,mode))
                exchanges+=poll(current)+[(f'setvol {level}\n'.encode(),b'OK\n')]+poll(level)
                current=level
            self.server(exchanges);self.assertEqual(self.lib.test_poll(),1)
            for n,index in enumerate(indices):
                self.assertEqual(self.lib.test_step_confirm(1 if n<count else -1),1)
                self.assertAlmostEqual(self.lib.test_fraction(),self.target(index,mode),places=7)
            self.threads[-1].join()

    def test_squared_mode_quantized_echo_retains_logical_position(self):
        self.lib.test_set_volume_steps(64);poll=self.volume_poll
        t33=self.percent_text(self.target(33,64));t34=self.percent_text(self.target(34,64))
        self.server(poll('25.4')+[(f'setvol {t33}\n'.encode(),b'OK\n')]+poll('26.1951')+
                    poll('26.1951')+[(f'setvol {t34}\n'.encode(),b'OK\n')]+poll('27.8319')+
                    poll('27.8319')+[(f'setvol {t33}\n'.encode(),b'OK\n')]+poll('26.1951')+poll('30'))
        for direction,actual in [(1,.261951),(1,.278319),(-1,.261951)]:
            self.assertEqual(self.lib.test_step_confirm(direction),1)
            self.assertAlmostEqual(self.lib.test_fraction(),actual,places=6)
        self.assertEqual(self.lib.test_poll(),1);self.assertEqual(self.lib.test_grid_target(),-1)

    def test_integer_only_server_retry_keeps_actual_readback(self):
        self.lib.test_set_volume_steps(64);poll=self.volume_poll
        t33=self.percent_text(self.target(33,64))
        self.server(poll(25)+[(f'setvol {t33}\n'.encode(),b'ACK [2@0] {setvol} need integer\n'),
                             (b'setvol 27\n',b'OK\n')]+poll(27)+poll(27)+[(b'setvol 28\n',b'OK\n')]+poll(28))
        self.assertEqual(self.lib.test_step_confirm(1),1);self.assertAlmostEqual(self.lib.test_fraction(),.27,places=6)
        self.assertEqual(self.lib.test_step_confirm(1),1);self.assertAlmostEqual(self.lib.test_fraction(),.28,places=6)

    def test_permission_error_is_not_retried(self):
        self.lib.test_set_volume_steps(64)
        value=self.percent_text(self.target(33,64))
        self.server(self.volume_poll(25)+[(f'setvol {value}\n'.encode(),b'ACK [4@0] {setvol} permission denied\n')])
        self.assertEqual(self.lib.test_step_confirm(1),0)

    def test_remote_snapshot_contains_raw_target_scale_and_completed_request(self):
        self.lib.test_set_volume_steps(64);poll=self.volume_poll
        value=self.percent_text(self.target(33,64))
        self.server(poll('25.4')+[(f'setvol {value}\n'.encode(),b'OK\n')]+poll('26.1951'))
        self.assertEqual(self.lib.test_remote_confirm(8,1),1)
        self.assertAlmostEqual(self.lib.test_fraction(),.261951,places=6)
        self.assertEqual(self.lib.test_grid_target(),self.target(33,64))
        self.assertEqual(self.lib.test_grid_steps(),64);self.assertEqual(self.lib.test_volume_request(),8)

    def test_coloplay_ignores_controller_step_fields_for_all_scales(self):
        for mode,old_field,level,usage,count,index in [(16,64,67,233,1,14),(64,16,67,233,1,53),(16,64,67,234,1,12),(2,64,61,233,1,31),(5,16,61,234,1,12),(64,16,0,234,1,0),(16,64,99,233,10,16)]:
            self.lib.test_set_volume_steps(mode)
            value=self.percent_text(self.target(index,mode))
            self.server(self.volume_poll(level)+[(f'setvol {value}\n'.encode(),b'OK\n')])
            self.assertIn(self.lib.test_coloplay(2,usage,count,old_field,50,1,7),(2,4))
            self.threads[-1].join()

    def test_coloplay_batches_count_every_scale_detent(self):
        self.lib.test_set_volume_steps(64)
        for usage,index in [(233,35),(234,29)]:
            value=self.percent_text(self.target(index,64))
            self.server(self.volume_poll('25.4')+[(f'setvol {value}\n'.encode(),b'OK\n')])
            self.assertEqual(self.lib.test_coloplay(2,usage,3,1,0,1,7),2);self.threads[-1].join()

    def test_coloplay_rejects_queued_command_after_transport_handover(self):
        self.assertEqual(self.lib.test_coloplay_retired_epoch(),3)

    def test_coloplay_rejects_old_session_and_missing_mixer(self):
        self.assertEqual(self.lib.test_coloplay(2,233,1,64,50,1,999),3)
        self.server(self.volume_poll(-1));self.assertEqual(self.lib.test_coloplay(2,233,1,64,50,1,7),5)
        self.threads[-1].join();self.peer.setblocking(False)
        with self.assertRaises(BlockingIOError):self.peer.recv(1)

    def test_coloplay_media_controls_preserve_explicit_play_pause_semantics(self):
        for usage,play,expected in [(205,1,b'pause 1\n'),(205,2,b'pause 0\n'),(176,0,b'play\n'),(177,1,b'pause 1\n'),(181,1,b'next\n'),(182,1,b'previous\n'),(183,1,b'stop\n')]:
            self.server([(expected,b'OK\n')]);self.assertEqual(self.lib.test_coloplay(1,usage,1,0,50,play,7),2)

    def test_coloplay_seek_release_does_not_touch_the_legacy_hold_path(self):
        self.server([(b'seekcur +5\n',b'OK\n')]);self.assertEqual(self.lib.test_coloplay(1,179,1,1,50,1,7),2)
        self.assertEqual(self.lib.test_coloplay_seeking(),1);self.assertEqual(self.lib.test_coloplay(4,0,1,0,50,1,7),2)
        self.assertEqual(self.lib.test_coloplay_seeking(),0)

    def test_native_controls_use_configured_scale_and_fresh_volume(self):
        for mode,index in [(16,11),(64,40),(2,19),(5,8)]:
            self.lib.test_set_volume_steps(mode);value=self.percent_text(self.target(index,mode))
            self.server(self.volume_poll(37)+[(f'setvol {value}\n'.encode(),b'OK\n')])
            self.assertEqual(self.lib.test_toggle(b'@volume_up',0,50,0),1);self.threads[-1].join()

    def test_volume_clamps_at_both_ends_and_missing_mixer_sends_nothing(self):
        for command,level in [(b'@volume_up',100),(b'@volume_down',0)]:
            self.server(self.volume_poll(level)+[(f'setvol {level}\n'.encode(),b'OK\n')])
            self.assertEqual(self.lib.test_toggle(command,0,level,0),1);self.threads[-1].join()
        self.server(self.volume_poll(-1));self.assertEqual(self.lib.test_toggle(b'@volume_up',0,-1,0),1)
        self.assertIn(b'mixer is unavailable',self.lib.test_message())

    def test_mute_restores_on_selected_scale(self):
        self.lib.test_set_volume_steps(16)
        for command,volume,value in [(b'@mute',67,'0'),(b'@mute',0,'66.015625')]:
            self.server([(f'setvol {value}\n'.encode(),b'OK\n')]);self.assertEqual(self.lib.test_toggle(command,0,volume,0),1);self.threads[-1].join()

if __name__=='__main__': unittest.main()
