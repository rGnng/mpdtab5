"""Exercise production album actions at the asynchronous MPD queue boundary."""
from pathlib import Path
import subprocess
import re
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


def function(source,name):
    match=re.search(r'^(?:static )?void '+name+r'\([^;]*?\)\s*\{',source,re.M)
    assert match,'Production function missing: '+name
    depth,end=1,match.end()
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}')
        end+=1
    return source[match.start():end]


class MpdPlaybackTests(unittest.TestCase):
    def test_shuffle_flag_travels_with_the_album_queue_request(self):
        source=(ROOT/'app/backend/mock_player.c').read_text()
        production='\n'.join(function(source,name) for name in [
            'play_ids_start','player_play_album','player_play_album_shuffled'])
        fixture=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define ESP_PLATFORM 1
#define PLAY_IDS_MAX 16
#define PLAYER_PLAYING 1
static uint64_t play_ids[16],queue[16];
static int queue_count,queue_index,elapsed_sec,status;
static unsigned queue_rev;
static bool shuffle;
static unsigned requests,separate_shuffle;
static bool requested_shuffle;
static int result;
static bool mpd_client_is_enabled(void) { return true; }
static int mpd_client_play_track_keys(const uint64_t *keys,int count,int start,bool shuffled) {
    assert(count==3 && start==0 && keys[0]==101 && keys[1]==102 && keys[2]==103);
    requests++;requested_shuffle=shuffled;return result;
}
static void player_set_shuffle(bool on) { (void)on;separate_shuffle++; }
static void bump_revision(void) {}
static int ids_album_from(uint64_t id,int from,uint64_t *out,int capacity) {
    assert(id==42 && from==0 && capacity>=3);
    for(int i=0;i<3;i++)out[i]=101+i;
    return 3;
}
'''
        main=r'''
int main(void) {
    player_play_album_shuffled(42);
    assert(requests==1 && requested_shuffle && separate_shuffle==0);
    player_play_album(42);
    assert(requests==2 && !requested_shuffle && separate_shuffle==0);
    // Rejected queue submissions cannot independently enable shuffle on the old queue.
    result=-1;player_play_album_shuffled(42);
    assert(requests==3 && requested_shuffle && separate_shuffle==0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            directory=Path(tmp)
            (directory/'playback.c').write_text(fixture+production+main)
            binary=directory/'playback'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wno-unused-function',
                            str(directory/'playback.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)


if __name__=='__main__':unittest.main()
