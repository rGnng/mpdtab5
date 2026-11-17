"""Production PID lookup, exercised without hardware."""
import ctypes
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r'^static [^\n]+\b' + name + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise AssertionError(name)
    depth, pos = 1, match.end()
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[match.start():pos]


class QueuePopupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('cc'):
            raise unittest.SkipTest('C compiler unavailable')
        cls.tmp = tempfile.TemporaryDirectory()
        player = (ROOT / 'app/backend/mock_player.c').read_text()
        ui = (ROOT / 'app/ui/ui.c').read_text()
        lookup = player[player.index('#define TRACK_ID_SLOTS'):player.index('/* iAP2 tags', player.index('#define TRACK_ID_SLOTS'))]
        prelude = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MAX_TRACKS 24000
#define MOCK_LARGE_BSS
#define LV_UNUSED(x) (void)(x)
#define UI_PERF_LOG(...) do { if(0) printf(__VA_ARGS__); } while(0)
#define NP_QUEUE_ROW_H 56
#define SCR_NOW_PLAYING 0
#define LV_OPA_TRANSP 0
static bool song_order_ready;
typedef struct { uint64_t id; char metadata[320]; } player_track_t;
static player_track_t tracks[MAX_TRACKS];
static int track_count;
'''
        exports = r'''
void reset(void) {memset(track_id_slots,0,sizeof(track_id_slots));track_id_index_ready=false;track_count=0;}
void add(uint64_t id) {tracks[track_count++].id=id;}
void build(void) {track_id_index_build();}
int lookup_id(uint64_t id) {return track_index_by_id(id);}
int lookup_ptr(uint64_t id) {const player_track_t *p=track_by_id(id);return p?(int)(p-tracks):-1;}
'''
        source=prelude+lookup+exports
        c = Path(cls.tmp.name) / 'queue.c'
        so = Path(cls.tmp.name) / 'queue.so'
        c.write_text(source)
        result = subprocess.run(['cc', '-std=c11', '-O2', '-shared', '-fPIC', '-Wall', '-Werror',
                                 str(c), '-o', str(so)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        cls.lib = ctypes.CDLL(str(so))
        cls.lib.add.argtypes = [ctypes.c_uint64]
        cls.lib.lookup_id.argtypes = [ctypes.c_uint64]
        cls.lib.lookup_ptr.argtypes = [ctypes.c_uint64]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.lib.reset()

    def test_full_library_lookup_and_missing_ids(self):
        ids = random.Random(17).sample(range(1 << 63, (1 << 63) + 1000000), 24000)
        for key in ids:
            self.lib.add(key)
        self.lib.build()
        for i, key in enumerate(ids):
            self.assertEqual(self.lib.lookup_id(key), i)
        self.assertEqual(self.lib.lookup_id(0), -1)
        self.assertEqual(self.lib.lookup_id((1 << 64) - 1), -1)
        self.assertEqual(self.lib.lookup_ptr(ids[-1]), 23999)

    def test_empty_index_and_duplicate_first_match(self):
        self.lib.build()
        self.assertEqual(self.lib.lookup_id(0), -1)
        self.lib.reset()
        for key in (0, 2**64-1, 9, 9):
            self.lib.add(key)
        self.assertEqual(self.lib.lookup_id(9), 2)  # pre-index fallback
        self.lib.build()
        self.assertEqual(self.lib.lookup_id(0), 0)
        self.assertEqual(self.lib.lookup_id(2**64-1), 1)
        self.assertEqual(self.lib.lookup_id(9), 2)
