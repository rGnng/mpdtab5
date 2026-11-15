"""Exercise production volume steps against a fake MPD server and the mock backend."""
import ctypes
import math
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class VolumeControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('cc'):
            raise unittest.SkipTest('C compiler unavailable')
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        source = (ROOT / 'app/backend/mock_player.c').read_text()
        start = re.search(r'^void player_step_volume\([^;]*?\)\s*\{', source, re.M)
        depth, end = 1, start.end()
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        production = source[start.start():end]
        boundary = r'''
#include <stdbool.h>
#include <string.h>
#include "volume_grid.h"
static float volume;
static volume_grid_cursor_t cursor;
static int revisions, count, commands[256];
static bool muted;
static int steps=16;
int player_volume_steps(void) { return steps; }
void set_steps(int n) { steps=volume_steps_normalize(n); }
float snap(float level,int n) { return volume_grid_snap(level,n); }
float display(float level,int n) { return volume_grid_display(level,n); }
float step(float level,int delta,int n) { return volume_grid_step(level,delta,n); }
void cursor_reset(void) { memset(&cursor,0,sizeof(cursor)); }
void cursor_request(float level,int n) { volume_grid_requested(&cursor,level,n); }
void cursor_observe(float level) { volume_grid_observe(&cursor,level); }
float cursor_next(float level,int delta,int n) { return volume_grid_next(&cursor,level,delta,n); }
int cursor_active(void) { return cursor.active; }

#if defined(ESP_PLATFORM)
static bool mpd_client_is_enabled(void) { return true; }
static int mpd_client_hid_command(const char *name) {
    if(count < 256) commands[count++] = strcmp(name, "volume_up") == 0 ? 1 : -1;
    return 0;
}
#endif
static void bump_revision(void) { revisions++; }
void reset(float v) { volume=v; muted=true; revisions=count=0; }
float value(void) { return volume; }
int is_muted(void) { return muted; }
int command_count(void) { return count; }
int command(int i) { return commands[i]; }
int revision(void) { return revisions; }
'''
        (work / 'volume.c').write_text(boundary + production)
        cls.backends = {}
        for name, flags in [('mpd', ['-DESP_PLATFORM']), ('mock', [])]:
            library = work / f'{name}.so'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-shared', '-fPIC', '-I'+str(ROOT/'app/backend'), *flags, str(work / 'volume.c'),
                            '-o', str(library)], check=True)
            lib = ctypes.CDLL(str(library))
            lib.player_step_volume.argtypes = [ctypes.c_int]
            lib.player_step_volume.restype = None
            lib.reset.argtypes = [ctypes.c_float]
            lib.value.restype=ctypes.c_float
            lib.snap.argtypes=[ctypes.c_float,ctypes.c_int];lib.snap.restype=ctypes.c_float
            lib.step.argtypes=[ctypes.c_float,ctypes.c_int,ctypes.c_int];lib.step.restype=ctypes.c_float
            lib.cursor_request.argtypes=[ctypes.c_float,ctypes.c_int]
            lib.cursor_observe.argtypes=[ctypes.c_float]
            lib.cursor_next.argtypes=[ctypes.c_float,ctypes.c_int,ctypes.c_int];lib.cursor_next.restype=ctypes.c_float
            lib.display.argtypes=[ctypes.c_float,ctypes.c_int];lib.display.restype=ctypes.c_float
            cls.backends[name] = lib

    def setUp(self):
        for lib in self.backends.values():lib.set_steps(16)

    @staticmethod
    def targets(mode):
        count={16:16,64:64,2:50,5:20}[mode]
        return [ctypes.c_float((i/count)**2 if mode in (16,64) else i/count).value for i in range(count+1)]

    def test_all_lookup_targets_round_trip_and_all_percentages_snap(self):
        lib=self.backends['mock']
        for mode in [16,64,2,5]:
            table=self.targets(mode);count=len(table)-1
            for i,target in enumerate(table):
                self.assertEqual(lib.snap(target,mode),target)
                self.assertEqual(lib.step(target,1,mode),table[min(count,i+1)])
                self.assertEqual(lib.step(target,-1,mode),table[max(0,i-1)])
                self.assertAlmostEqual(lib.display(target,mode),i/count,places=6)
            for percentage in range(101):
                raw=ctypes.c_float(percentage/100).value
                nearest=min(range(len(table)),key=lambda i:(abs(raw-table[i]),-i))
                self.assertEqual(lib.snap(raw,mode),table[nearest])
                for delta in [-100,-2,-1,0,1,2,100]:
                    index=nearest
                    if mode in (2,5):
                        if delta>0 and raw<table[index]-.000001:index-=1
                        if delta<0 and raw>table[index]+.000001:index+=1
                    self.assertEqual(lib.step(raw,delta,mode),table[max(0,min(count,index+delta))])

    def test_default_invalid_bounds_and_squared_low_level_precision(self):
        lib=self.backends['mock']
        self.assertEqual(lib.step(0,1,64),1/4096)
        self.assertEqual(lib.step(0,1,16),1/256)
        self.assertEqual(lib.snap(-10,16),0);self.assertEqual(lib.snap(1.1,64),1)
        self.assertEqual(lib.snap(.37,0),lib.snap(.37,64))
        self.assertEqual(lib.step(.61,1,2),ctypes.c_float(.62).value)
        self.assertEqual(lib.step(.61,-1,2),ctypes.c_float(.60).value)
        self.assertEqual(lib.step(.61,1,5),ctypes.c_float(.65).value)
        self.assertEqual(lib.step(.61,-1,5),ctypes.c_float(.60).value)

    def test_squared_hardware_readback_maps_to_original_display_position(self):
        lib=self.backends['mock']
        for mode in [16,64]:
            count=mode
            for index,target in enumerate(self.targets(mode)):
                hardware=round(math.sqrt(target)*127)
                actual=(hardware/127)**2
                self.assertAlmostEqual(lib.display(actual,mode),index/count,places=6)

    def test_quantized_readback_anchors_raw_target_separately(self):
        lib=self.backends['mock'];lib.cursor_reset()
        target=(33/64)**2
        lib.cursor_request(target,64);lib.cursor_observe(.261951)
        self.assertEqual(lib.cursor_next(.261951,1,64),(34/64)**2)
        self.assertEqual(lib.cursor_next(.261951,-1,64),(32/64)**2)
        self.assertAlmostEqual(lib.display(.261951,64),33/64,places=6)
        lib.cursor_observe(.30);self.assertFalse(lib.cursor_active())
        for bad in [-1,float('nan')]:
            lib.cursor_request(target,64);lib.cursor_observe(bad);self.assertFalse(lib.cursor_active())

    def test_mpd_sends_one_native_step_per_detent(self):
        lib = self.backends['mpd']
        lib.reset(.72)
        for direction in [1, 1, -1, -1, 1]:
            lib.player_step_volume(direction)
        self.assertEqual(lib.command_count(), 5)
        self.assertEqual([lib.command(i) for i in range(5)], [1, 1, -1, -1, 1])

    def test_mpd_steps_never_stop_at_a_local_estimated_limit(self):
        lib = self.backends['mpd']
        for estimate in [0, .72, 1]:
            lib.reset(estimate)
            for direction in [1] * 40 + [-1] * 40:
                lib.player_step_volume(direction)
            self.assertEqual(lib.command_count(), 80)
            self.assertEqual([lib.command(i) for i in range(80)], [1] * 40 + [-1] * 40)

    def test_zero_is_a_no_op_for_both_backends(self):
        for lib in self.backends.values():
            lib.reset(.72)
            lib.player_step_volume(0)
            self.assertEqual(lib.value(), ctypes.c_float(.72).value)
            self.assertEqual(lib.command_count(), 0)
            self.assertEqual(lib.revision(), 0)

    def test_mock_adjusts_in_both_directions_and_clamps(self):
        lib = self.backends['mock']
        lib.reset(.72)
        lib.player_step_volume(1)
        self.assertEqual(lib.value(), (15/16)**2)
        self.assertFalse(lib.is_muted())
        lib.player_step_volume(-1)
        self.assertEqual(lib.value(), (14/16)**2)
        for _ in range(40):
            lib.player_step_volume(1)
        self.assertEqual(lib.value(), 1)
        for _ in range(40):
            lib.player_step_volume(-1)
        self.assertEqual(lib.value(), 0)
        self.assertEqual(lib.command_count(), 0)


if __name__ == '__main__':
    unittest.main()
