"""Exercise every MPD library view and its playback list using the real model."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include "app/backend/mock_player.c"
void model_reset(void) {
    track_count=album_count=artist_count=playlist_count=queue_count=0;
    queue_index=0;status=PLAYER_STOPPED;real_library=mpd_library=true;
    artist_order_ready=false;artist_order_count=0;track_id_index_ready=false;
    membership_index_ready=false;song_order_ready=false;live_album_ready=false;
    memset(track_starred,0,sizeof(track_starred));memset(live_tracks,0,sizeof(live_tracks));
    memset(album_seen,0,sizeof(album_seen));media_index_clear(&live_afc);
    live_in_batch=live_dirty=live_publish=live_suspended=false;
    live_batch=0;album_seen_next_seq=1;
    album_sort_field=song_sort_field=0;album_sort_desc=song_sort_desc=true;
    track_id_index_build();membership_index_build();
}
void model_add(uint64_t id,uint64_t album,const char *album_name,const char *artist,
               const char *title,uint32_t date,unsigned year,unsigned disc,unsigned number) {
    player_media_item_t p={.pid=id,.album_pid=album,.date_added=date,.year=year,.disc_no=disc,.track_no=number,
        .duration_ms=240000,.fields=PLAYER_MEDIA_MPD|(1u<<1)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<9)|(1u<<12)|(1u<<14)};
    snprintf(p.title,sizeof(p.title),"%s",title);snprintf(p.album,sizeof(p.album),"%s",album_name);
    snprintf(p.artist,sizeof(p.artist),"%s",artist);snprintf(p.album_artist,sizeof(p.album_artist),"%s",artist);
    player_library_update(&p);
}
uint64_t album_id(int i) { player_album_t a;return player_album_get(i,&a)?a.id:0; }
uint32_t album_date(int i) { player_album_t a;return player_album_get(i,&a)?a.date_added:0; }
uint64_t song_id(int i) { player_track_t t;return player_song_get(i,&t)?t.id:0; }
uint64_t album_track_id(uint64_t id,int i) { player_track_t t;return player_album_track_get(id,i,&t)?t.id:0; }
uint64_t artist_track_id(uint64_t id,int i) { player_track_t t;return player_artist_song_get(id,i,&t)?t.id:0; }
uint64_t artist_id(int i) { player_artist_t a;return player_artist_get(i,&a)?a.id:0; }
const char *artist_name(int i) { static player_artist_t a;return player_artist_get(i,&a)?a.name:""; }
uint64_t queue_id(int i) { player_track_t t;return player_queue_at(i,&t)?t.id:0; }
int current_status(void) { player_snapshot_t s;player_get_snapshot(&s);return s.status; }
unsigned elapsed(void) { player_snapshot_t s;player_get_snapshot(&s);return s.elapsed_sec; }
int is_starred(void) { player_snapshot_t s;player_get_snapshot(&s);return s.starred; }
'''

class MPDViewsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(); directory=Path(cls.tmp.name)
        (directory/'model.c').write_text(HARNESS)
        subprocess.run(['cc','-std=gnu11','-O2','-shared','-fPIC',
            '-I'+str(ROOT),'-I'+str(ROOT/'third_party/cjson'),str(directory/'model.c'),
            str(ROOT/'app/backend/media_index.c'),str(ROOT/'third_party/cjson/cJSON.c'),
            '-lm','-o',str(directory/'model.so')],check=True)
        cls.lib=C.CDLL(str(directory/'model.so'))
        cls.lib.model_add.argtypes=[C.c_uint64,C.c_uint64,C.c_char_p,C.c_char_p,C.c_char_p,C.c_uint32,C.c_uint,C.c_uint,C.c_uint]
        for name in ['album_id','song_id','artist_id','queue_id']:
            getattr(cls.lib,name).restype=C.c_uint64
        for name in ['album_track_id','artist_track_id']:
            getattr(cls.lib,name).argtypes=[C.c_uint64,C.c_int];getattr(cls.lib,name).restype=C.c_uint64
        cls.lib.artist_name.restype=C.c_char_p
        for name in ['player_play_song','player_play_album','player_play_artist','player_library_delete']:
            getattr(cls.lib,name).argtypes=[C.c_uint64]
        cls.lib.player_play_artist_from.argtypes=[C.c_uint64,C.c_uint64]
        cls.lib.player_play_album_from.argtypes=[C.c_uint64,C.c_int]
        for name in ['player_sort_albums','player_sort_songs']:
            getattr(cls.lib,name).argtypes=[C.c_int,C.c_bool]
        cls.lib.player_library_playlist_item.argtypes=[C.c_int,C.c_char_p,C.c_uint64]
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def setUp(self):self.lib.model_reset()
    def add(self,id,album=10,album_name='Album',artist='Artist',title=None,date=1000,year=2020,disc=0,number=1):
        self.lib.model_add(id,album,album_name.encode(),artist.encode(),(title or f'Song {id}').encode(),date,year,disc,number)
    def finish(self):self.lib.player_library_complete()
    def queue(self):return [self.lib.queue_id(i) for i in range(self.lib.player_queue_count())]
    def fixture(self):
        self.add(1,10,'Beta','Zulu','Two',2000,2022,0,2)
        self.add(2,20,'Alpha','Alpha','One',1000,2020,0,1)
        self.add(3,10,'Beta','Zulu','Three',3000,2022,0,3)
        self.add(4,30,'Gamma','Zulu','Four',4000,2024,0,1)
        self.add(5,10,'Beta','Zulu','Five',2000,2022,0,1)
        self.finish()
    def test_album_date_name_year_both_directions(self):
        self.fixture()
        for field,ascending in [(0,[20,10,30]),(1,[20,10,30]),(2,[20,10,30])]:
            for desc,expected in [(False,ascending),(True,list(reversed(ascending)))]:
                self.lib.player_sort_albums(field,desc)
                self.assertEqual([self.lib.album_id(i) for i in range(3)],expected)
        self.assertEqual([self.lib.album_date(i) for i in range(3)],[4000,3000,1000])
    def test_tied_dates_and_years_still_obey_direction(self):
        for i,title in enumerate(['Charlie','Alpha','Beta']):self.add(i+1,10+i,title,date=0,year=0)
        self.finish()
        for field in [0,2]:
            self.lib.player_sort_albums(field,False);a=[self.lib.album_id(i) for i in range(3)]
            self.lib.player_sort_albums(field,True);b=[self.lib.album_id(i) for i in range(3)]
            self.assertEqual(b,list(reversed(a)))
    def test_sort_choice_before_first_import_is_applied(self):
        self.lib.player_sort_albums(1,True)
        self.add(1,10,'Alpha');self.add(2,20,'Zulu');self.finish()
        self.assertEqual(self.lib.album_id(0),20)
    def test_resync_retains_dates_and_order(self):
        self.fixture();self.lib.player_sort_albums(0,True)
        before=[self.lib.album_id(i) for i in range(3)]
        self.lib.player_library_reset();self.fixture()
        self.assertEqual([self.lib.album_id(i) for i in range(3)],before)
    def test_album_track_number_sort_without_disc_tags_and_play_from_here(self):
        self.fixture()
        expected=[5,1,3]
        self.assertEqual([self.lib.album_track_id(10,i) for i in range(3)],expected)
        self.lib.player_play_album(10);self.assertEqual(self.queue(),expected)
        self.lib.player_play_album_from(10,1);self.assertEqual(self.queue(),[1,3])
    def test_missing_disc_is_disc_one_and_comparator_is_consistent(self):
        for i,disc,number in [(1,2,1),(2,0,2),(3,1,1),(4,0,3)]:self.add(i,disc=disc,number=number)
        self.finish();self.lib.player_play_album(10)
        self.assertEqual(self.queue(),[3,2,4,1])
    def test_artists_are_alphabetic_and_empty_artists_disappear(self):
        self.fixture()
        self.assertEqual([self.lib.artist_name(i) for i in range(self.lib.player_artist_count())],[b'Alpha',b'Zulu'])
        stable=self.lib.artist_id(1)
        self.lib.player_library_delete(2);self.finish()
        self.assertEqual(self.lib.player_artist_count(),1)
        self.assertEqual(self.lib.artist_id(0),stable)
    def test_artist_playback_matches_grouped_visible_members(self):
        self.fixture();id=self.lib.artist_id(1)
        visible=[self.lib.artist_track_id(id,i) for i in range(4)]
        self.assertEqual(visible,[5,1,3,4])
        self.lib.player_play_artist(id);self.assertEqual(self.queue(),visible)
        self.lib.player_play_artist_from(id,3);self.assertEqual(self.queue(),[3,4,5,1])
    def test_song_playback_matches_every_displayed_sort(self):
        self.fixture()
        for field in range(3):
            for desc in [False,True]:
                self.lib.player_sort_songs(field,desc)
                visible=[self.lib.song_id(i) for i in range(5)]
                self.lib.player_play_song(visible[2])
                self.assertEqual(self.queue(),visible[2:]+visible[:2])
    def test_selection_over_512_tracks_is_complete(self):
        for i in range(700):self.add(i+1,number=i+1)
        self.finish();self.lib.player_play_album(10)
        self.assertEqual(self.lib.player_queue_count(),700)
        self.assertEqual(self.lib.queue_id(699),700)
    def test_playlists_preserve_duplicates_and_start_position(self):
        self.fixture();self.lib.player_library_playlists_reset()
        self.lib.player_library_playlist_item(0,b'Mix',0)
        for id in [4,2,4,1]:self.lib.player_library_playlist_item(0,None,id)
        self.lib.player_play_playlist(1);self.assertEqual(self.queue(),[4,2,4,1])
        self.lib.player_play_playlist_from(1,2);self.assertEqual(self.queue(),[4,1])
    def test_playlist_changes_do_not_invalidate_catalog(self):
        self.fixture();self.lib.player_library_poll()
        catalog=self.lib.player_library_revision();playlists=self.lib.player_playlist_revision()
        self.lib.player_library_playlists_reset()
        self.lib.player_library_playlist_item(0,b'Mix',0)
        self.lib.player_library_playlist_item(0,None,1)
        self.assertEqual(self.lib.player_library_revision(),catalog)
        self.assertGreater(self.lib.player_playlist_revision(),playlists)
        self.assertFalse(self.lib.player_library_poll())
        self.assertEqual(self.lib.player_playlist_track_count(1),1)

    def test_empty_playlist_does_not_replace_existing_queue(self):
        self.fixture();self.lib.player_play_album(10);before=self.queue()
        self.lib.player_play_playlist(0);self.assertEqual(self.queue(),before)
    def test_now_playing_queue_seek_and_local_starred_view(self):
        self.fixture();self.lib.player_play_album(10)
        self.assertEqual(self.lib.current_status(),1)
        self.lib.player_play_pause();self.assertEqual(self.lib.current_status(),2)
        self.lib.player_play_pause();self.assertEqual(self.lib.current_status(),1)
        self.lib.player_seek_sec(70);self.assertEqual(self.lib.elapsed(),70)
        self.lib.player_queue_jump(2);self.assertEqual(self.lib.player_queue_index(),2)
        self.lib.player_toggle_star();self.assertTrue(self.lib.is_starred())
        self.assertEqual(self.lib.player_playlist_track_count(0),1)
        self.lib.player_play_playlist(0);self.assertEqual(self.queue(),[3])
        self.lib.player_toggle_star();self.assertEqual(self.lib.player_playlist_track_count(0),0)

if __name__=='__main__':unittest.main()
