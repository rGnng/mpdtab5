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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define TRACK_LIMIT 24000
#define QUEUE_LIMIT 24000
#define URI_BYTES (3*1024*1024)
#define URI_SLOTS 65536
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define heap_caps_malloc(n,caps) malloc(n)
#define vTaskDelay(n) ((void)(n))
static mpd_wire_t wire;
static mpd_state_t state;
static int muted_volume=50, mpd_playlist_version;
static bool remote_repeat,remote_single,mpd_updating;
static unsigned reported_queue_size, database_songs;
static char database_revision[64];
static bool unpaged_server;
static player_media_item_t items[1000];
static int item_count,playlist_count;
static uint64_t playlist_keys[1024];
static uint32_t parsed_queue_ids[QUEUE_LIMIT];
static char diagnostic[200];
static void take(void) {}
static void give(void) {}
void player_library_reset(void) { item_count=0; }
void player_library_complete(void) {}
static void message(const char *text) { snprintf(diagnostic,sizeof(diagnostic),"%s",text); }
bool mpd_client_get_state(mpd_state_t *out) { *out=state; return true; }
void player_library_update(const player_media_item_t *item) { if(item_count<1000) items[item_count++]=*item; }
void player_library_playlist_item(int index,const char *name,uint64_t key) {
    (void)index; (void)name;
    if(playlist_count<1024) playlist_keys[playlist_count++]=key;
}
'''
SUFFIX=r'''
void test_init(int fd) {
    memset(uri_slots,0,sizeof(uri_slots)); uri_count=uri_used=0;
    memset(&state,0,sizeof(state)); item_count=playlist_count=0;
    unpaged_server=false; database_songs=0;
    diagnostic[0]=0; mpd_wire_init(&wire); mpd_wire_attach(&wire,fd,1000);
}
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
int test_toggle(const char *name,int repeat,int volume,int shuffle) {
    state.repeat=repeat; state.volume=volume; state.shuffle=shuffle;
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
        globals_=uri_type+'''
static uri_entry_t uris[TRACK_LIMIT];
static uint16_t uri_slots[URI_SLOTS];
static char uri_pool[URI_BYTES];
static unsigned uri_count,uri_used;
'''
        functions='\n'.join(function(SOURCE,name) for name in [
            'uri_lookup','uri_insert','song_reset','song_finish','song_field',
            'status_field','stat_field','simple','sync_library','play_selection','run_command'])
        (directory/'client.c').write_text(PREFIX+globals_+command_type+song_type+functions+SUFFIX)
        subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Wno-unused-function','-shared','-fPIC',
                        '-I'+str(directory),'-I'+str(ROOT/'app/mpd'),'-I'+str(ROOT/'app/backend'),
                        '-I'+str(ROOT/'firmware/main'),str(directory/'client.c'),
                        str(ROOT/'app/mpd/mpd_wire.c'),'-o',str(directory/'client.so')],check=True)
        cls.lib=C.CDLL(str(directory/'client.so'))
        cls.lib.test_insert.argtypes=[C.c_char_p]; cls.lib.test_insert.restype=C.c_uint64
        cls.lib.test_run.argtypes=[C.c_int,C.POINTER(C.c_uint64),C.c_int,C.c_int]
        cls.lib.test_toggle.argtypes=[C.c_char_p,C.c_int,C.c_int,C.c_int]
        for name in ['test_track_key','test_album_key']:
            getattr(cls.lib,name).restype=C.c_uint64
        for name in ['test_title','test_artist','test_message']:
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

    def test_mute_restores_actual_volume_and_shuffle_uses_server_state(self):
        for command,vol,shuffle,expected in [(b'@mute',67,0,b'setvol 0\n'),(b'@mute',0,0,b'setvol 67\n'),(b'@shuffle',50,1,b'random 0\n')]:
            self.server([(expected,b'OK\n')])
            self.assertEqual(self.lib.test_toggle(command,0,vol,shuffle),1)
            self.threads[-1].join()

if __name__=='__main__': unittest.main()
