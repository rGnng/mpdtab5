"""Compile the firmware snapshot conversion used by Now Playing/context menus."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_album_view import function

ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'app/backend/mock_player.c').read_text()
PREFIX=r'''
#include "player.h"
#include "mpd_client.h"
#include "text_util.h"
#include "volume_grid.h"
int player_volume_steps(void){return 64;}
#include <stdio.h>
#include <string.h>
static player_track_t tracks[1];
static bool track_starred[1]={true};
uint32_t player_library_revision(void) { return 1; }
static void copy_str(char *dst,size_t cap,const char *src) { text_copy(dst,cap,src); }
static uint64_t iap2_track_id(const mpd_state_t *s) { return s->track_key; }
static uint64_t iap2_album_id(const mpd_state_t *s) { return s->album_pid; }
static int track_index_by_id(uint64_t id) { return id==101 ? 0 : -1; }
static int track_index_by_meta(const char *title,const char *album) { (void)title;(void)album;return -1; }
'''
SUFFIX=r'''
int star_value(int remote,int local) {
    mpd_state_t state={.track_key=101,.starred=remote};
    track_starred[0]=local;player_snapshot_t out;iap2_snapshot(&out,&state);return out.starred;
}

unsigned long value(int which,int empty) {
    tracks[0]=(player_track_t){.id=101,.album_id=88,.artist_id=77,.track_no=3,.disc_no=2,.year=2024};
    mpd_state_t state={.connected=true,.identified=true,.play_status=2,.volume=42,.battery=-1,.starred=true};
    if(!empty) { state.track_key=101;state.album_pid=88;strcpy(state.title,"Current track"); }
    player_snapshot_t s;iap2_snapshot(&s,&state);
    switch(which) {
    case 0:return s.track.artist_id;
    case 1:return s.track.album_id;
    case 2:return s.track.track_no;
    case 3:return s.track.disc_no;
    case 4:return s.track.year;
    case 5:return s.starred;
    case 6:return s.status;
    default:return s.track.id;
    }
}
'''
class SnapshotTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();path=Path(cls.tmp.name)
        (path/'esp_err.h').write_text('typedef int esp_err_t;\n')
        (path/'snapshot.c').write_text(PREFIX+function(SOURCE,'iap2_snapshot')+SUFFIX)
        subprocess.run(['cc','-shared','-fPIC','-std=gnu11','-I'+str(path),'-I'+str(ROOT/'app/backend'),'-I'+str(ROOT/'firmware/main'),str(path/'snapshot.c'),'-o',str(path/'snapshot.so')],check=True)
        cls.lib=C.CDLL(str(path/'snapshot.so'));cls.lib.value.restype=C.c_ulong
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def test_current_track_preserves_catalog_relationships(self):
        self.assertEqual([self.lib.value(i,0) for i in range(6)],[77,88,3,2,2024,1])
    def test_server_star_flag_overrides_local_file_state(self):
        self.assertEqual(self.lib.star_value(0,1),0)
        self.assertEqual(self.lib.star_value(1,0),1)
    def test_paused_track_keeps_its_artist_and_pause_state(self):
        self.assertEqual(self.lib.value(0,0),77)
        self.assertEqual(self.lib.value(6,0),2)
    def test_empty_playback_does_not_retain_stale_track_relationships(self):
        self.lib.value(0,0)
        self.assertEqual([self.lib.value(i,1) for i in range(6)],[0,0,0,0,0,0])

if __name__=='__main__':unittest.main()
