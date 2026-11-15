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
static float muted_volume=0.5f,volume_target=-1.0f;
static int server_volume=-1,mpd_playlist_version;
static mpd_config_t config;
static char current_uri[4096];
static uint32_t coloplay_session=7;static uint8_t coloplay_seek;static int64_t coloplay_seek_deadline,coloplay_next_seek;
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
    volume_steps=16;volume_target=-1.0f;server_volume=-1;muted_volume=0.5f;
    complete_count=0;
    diagnostic[0]=0; mpd_wire_init(&wire); mpd_wire_attach(&wire,fd,1000);
}
int test_sync_queue(unsigned size,unsigned budget) {
    reported_queue_size=size; allocation_limit=budget; largest_allocation=0;
    return sync_queue();
}
unsigned test_allocation(void) { return largest_allocation; }
int test_queue_count(void) { return queue_size; }
int test_completions(void) { return complete_count; }
int test_queue_pending(void) { return queue_refresh_pending; }
int test_sync_stars(unsigned budget) { allocation_limit=budget;return sync_stars(); }
int test_sync_playlists(void) { return sync_playlists(); }
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
int test_coloplay(int kind,int usage,int count,int parameter,int level,int play,uint32_t session) {
    command_t c={.kind=CMD_COLOPLAY,.remote_result=CP_APPLIED};
    cp_header(c.coloplay,kind,1,session);cp_put16(c.coloplay+8,usage);cp_put16(c.coloplay+10,count);cp_put16(c.coloplay+12,parameter);
    server_volume=level;state.volume=level<0?-1.0f:level/100.0f;state.play_status=play;coloplay_session=7;
    if(!run_command(&c)){return CP_FAILED;}return c.remote_result;
}
float test_fraction(void) { return state.volume; }
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
static uri_entry_t uris[TRACK_LIMIT];
static uint16_t uri_slots[URI_SLOTS];
static char uri_pool[URI_BYTES];
static unsigned uri_count,uri_used;
'''
        functions='\n'.join(function(SOURCE,name) for name in [
            'publish','uri_lookup','uri_insert','song_reset','song_finish','song_field',
            'status_field','stat_field','simple','sync_library','sync_queue','star_position',
            'sync_stars','change_star','playlist_field','sync_playlists','play_selection','set_volume','run_command','poll_state'])
        (directory/'client.c').write_text(PREFIX+globals_+command_type+song_type+functions+SUFFIX)
        subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Wno-unused-function','-shared','-fPIC',
                        '-I'+str(directory),'-I'+str(ROOT/'app/mpd'),'-I'+str(ROOT/'app/backend'),
                        '-I'+str(ROOT/'firmware/main'),str(directory/'client.c'),
                        str(ROOT/'app/mpd/mpd_wire.c'),'-o',str(directory/'client.so')],check=True)
        cls.lib=C.CDLL(str(directory/'client.so'))
        cls.lib.test_fraction.restype=C.c_float
        cls.lib.test_insert.argtypes=[C.c_char_p]; cls.lib.test_insert.restype=C.c_uint64
        cls.lib.test_run.argtypes=[C.c_int,C.POINTER(C.c_uint64),C.c_int,C.c_int]
        cls.lib.test_current_key.argtypes=[C.c_uint64]
        cls.lib.test_star_key.restype=C.c_uint64
        cls.lib.test_toggle.argtypes=[C.c_char_p,C.c_int,C.c_int,C.c_int]
        cls.lib.test_coloplay.argtypes=[C.c_int,C.c_int,C.c_int,C.c_int,C.c_int,C.c_int,C.c_uint32]
        for name in ['test_track_key','test_album_key']:
            getattr(cls.lib,name).restype=C.c_uint64
        for name in ['test_title','test_artist','test_message','test_playlist_name']:
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

    def test_known_album_queue_changes_do_not_republish_library(self):
        self.assertTrue(self.lib.test_insert(b'album/a.flac'))
        self.assertTrue(self.lib.test_insert(b'album/b.flac'))
        self.server([(b'playlistinfo\n',b'file: album/a.flac\nId: 1\nOK\n'),
                     (b'playlistinfo\n',b'file: album/b.flac\nId: 2\nOK\n')])
        self.assertEqual(self.lib.test_sync_queue(1,1024),1)
        self.assertEqual(self.lib.test_sync_queue(1,1024),1)
        self.assertEqual(self.lib.test_completions(),0)
        self.assertEqual(self.lib.test_item_count(),0)

    def test_new_queue_only_tracks_publish_catalog_once(self):
        self.server([(b'playlistinfo\n',b'file: stream/new\nTitle: New stream\nId: 3\nOK\n'),
                     (b'playlistinfo\n',b'file: stream/new\nTitle: New stream\nId: 3\nOK\n')])
        self.assertEqual(self.lib.test_sync_queue(1,1024),1)
        self.assertEqual(self.lib.test_completions(),1)
        self.assertEqual(self.lib.test_item_count(),1)
        self.assertEqual(self.lib.test_sync_queue(1,1024),1)
        self.assertEqual(self.lib.test_completions(),1)

    def test_album_queue_refresh_uses_actual_queue_size(self):
        data=b''.join(f'file: album/{i}.flac\nId: {i+1}\n'.encode() for i in range(12))
        self.server([(b'playlistinfo\n',data+b'OK\n')])
        self.assertEqual(self.lib.test_sync_queue(12,1024),1)
        self.assertEqual(self.lib.test_queue_count(),12)
        self.assertEqual(self.lib.test_queue_pending(),0)
        self.assertLessEqual(self.lib.test_allocation(),256)

    def test_low_memory_queue_refresh_preserves_connection_and_retries(self):
        self.server([(b'playlistinfo\n',b'file: old/a.flac\nId: 1\nfile: old/b.flac\nId: 2\nOK\n')])
        self.assertEqual(self.lib.test_sync_queue(2,1024),1)
        self.assertEqual(self.lib.test_queue_count(),2)
        self.assertEqual(self.lib.test_sync_queue(200,16),1)
        self.assertEqual(self.lib.test_queue_count(),2)
        self.assertEqual(self.lib.test_queue_pending(),1)
        self.assertIn(b'not enough memory',self.lib.test_message())
        self.server([(b'playlistinfo\n',b'file: album/a.flac\nId: 7\nOK\n')])
        self.assertEqual(self.lib.test_sync_queue(1,1024),1)
        self.assertEqual(self.lib.test_queue_count(),1)
        self.assertEqual(self.lib.test_queue_pending(),0)

    def test_queue_growth_between_status_and_reply_is_drained_then_retried(self):
        data=b''.join(f'file: album/{i}.flac\nId: {i+1}\n'.encode() for i in range(40))
        self.server([(b'playlistinfo\n',data+b'OK\n'),(b'playlistinfo\n',data+b'OK\n')])
        self.assertEqual(self.lib.test_sync_queue(1,1024),1)
        self.assertEqual(self.lib.test_queue_count(),0)
        self.assertEqual(self.lib.test_queue_pending(),1)
        self.assertEqual(self.lib.test_sync_queue(40,1024),1)
        self.assertEqual(self.lib.test_queue_count(),40)
        self.assertEqual(self.lib.test_queue_pending(),0)

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

    def test_fractional_positions_survive_integer_server_echo_and_external_changes(self):
        self.lib.test_set_volume_steps(64)
        def poll(level):
            return [(b'status\n',f'volume: {level}\nstate: pause\nOK\n'.encode()),(b'currentsong\n',b'OK\n')]
        exchanges=poll(0)
        for index in range(1,65):
            level=int(index*100/64+.5)
            exchanges += [(f'setvol {level}\n'.encode(),b'OK\n')]+poll(level)
        for index in range(63,-1,-1):
            level=int(index*100/64+.5)
            exchanges += [(f'setvol {level}\n'.encode(),b'OK\n')]+poll(level)
        exchanges += poll(43)
        self.server(exchanges)
        self.assertEqual(self.lib.test_poll(),1)
        for index in range(1,65):
            self.assertEqual(self.lib.test_step_confirm(1),1)
            self.assertEqual(self.lib.test_fraction(),index/64)
        for index in range(63,-1,-1):
            self.assertEqual(self.lib.test_step_confirm(-1),1)
            self.assertEqual(self.lib.test_fraction(),index/64)
        self.assertEqual(self.lib.test_poll(),1)
        self.assertAlmostEqual(self.lib.test_fraction(),.43,places=6)

    def test_coloplay_reuses_raw_mpd_volume_grid_without_changing_setting(self):
        for configured,requested,level,usage,count,expected in [(16,64,67,233,1,69),(64,16,67,233,1,75),(16,64,67,234,1,66),(64,16,0,234,1,0),(16,64,99,233,10,100),(64,1,67,233,1,69)]:
            self.lib.test_set_volume_steps(configured)
            self.server([(f'setvol {expected}\n'.encode(),b'OK\n')])
            self.assertIn(self.lib.test_coloplay(2,usage,count,requested,level,1,7),(2,4))

    def test_coloplay_rejects_old_session_and_unknown_mixer_without_wire_command(self):
        self.assertEqual(self.lib.test_coloplay(2,233,1,64,50,1,999),3)
        self.assertEqual(self.lib.test_coloplay(2,233,1,64,-1,1,7),5)
        self.peer.setblocking(False)
        with self.assertRaises(BlockingIOError):self.peer.recv(1)

    def test_coloplay_media_controls_preserve_explicit_play_pause_semantics(self):
        for usage,play,expected in [(205,1,b'pause 1\n'),(205,2,b'pause 0\n'),(176,0,b'play\n'),(177,1,b'pause 1\n'),(181,1,b'next\n'),(182,1,b'previous\n'),(183,1,b'stop\n')]:
            self.server([(expected,b'OK\n')])
            self.assertEqual(self.lib.test_coloplay(1,usage,1,0,50,play,7),2)

    def test_coloplay_seek_release_does_not_touch_the_legacy_hold_path(self):
        self.server([(b'seekcur +5\n',b'OK\n')])
        self.assertEqual(self.lib.test_coloplay(1,179,1,1,50,1,7),2)
        self.assertEqual(self.lib.test_coloplay_seeking(),1)
        self.assertEqual(self.lib.test_coloplay(4,0,1,0,50,1,7),2)
        self.assertEqual(self.lib.test_coloplay_seeking(),0)

    def test_volume_steps_snap_to_grid_before_one_notch(self):
        for steps,up,down in [(16,44,31),(64,39,36)]:
            self.lib.test_set_volume_steps(steps)
            for command,expected in [(b'@volume_up',up),(b'@volume_down',down)]:
                self.server([(f'setvol {expected}\n'.encode(),b'OK\n')])
                self.assertEqual(self.lib.test_toggle(command,0,37,0),1)
                self.threads[-1].join()

    def test_switching_volume_grid_uses_raw_server_level(self):
        self.server([(b'setvol 69\n',b'OK\n')])
        self.assertEqual(self.lib.test_volume_mode_change(),1)

    def test_volume_steps_clamp_without_disabling_controls(self):
        for command,level,expected in [(b'@volume_up',100,100),(b'@volume_down',0,0)]:
            self.server([(f'setvol {expected}\n'.encode(),b'OK\n')])
            self.assertEqual(self.lib.test_toggle(command,0,level,0),1)
            self.threads[-1].join()
        self.assertEqual(self.lib.test_toggle(b'@volume_up',0,-1,0),1)
        self.assertIn(b'mixer is unavailable',self.lib.test_message())

    def test_mute_restores_actual_volume_and_shuffle_uses_server_state(self):
        for command,vol,shuffle,expected in [(b'@mute',67,0,b'setvol 0\n'),(b'@mute',0,0,b'setvol 69\n'),(b'@shuffle',50,1,b'random 0\n')]:
            self.server([(expected,b'OK\n')])
            self.assertEqual(self.lib.test_toggle(command,0,vol,shuffle),1)
            self.threads[-1].join()

if __name__=='__main__': unittest.main()
