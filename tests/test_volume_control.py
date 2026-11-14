"""Exercise production volume steps against a fake MPD server and the mock backend."""
import ctypes
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
static int volume, revisions, count, commands[256];
static bool muted;
static int steps=16;
int player_volume_steps(void) { return steps; }
void set_steps(int n) { steps=volume_steps_normalize(n); }
int snap(int level,int n) { return volume_grid_snap(level,n); }
int step(int level,int delta,int n) { return volume_grid_step(level,delta,n); }
#if defined(ESP_PLATFORM)
static bool mpd_client_is_enabled(void) { return true; }
static int mpd_client_hid_command(const char *name) {
    if(count < 256) commands[count++] = strcmp(name, "volume_up") == 0 ? 1 : -1;
    return 0;
}
#endif
static void bump_revision(void) { revisions++; }
void reset(int v) { volume=v; muted=true; revisions=count=0; }
int value(void) { return volume; }
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
            lib.reset.argtypes = [ctypes.c_int]
            cls.backends[name] = lib

    def setUp(self):
        for lib in self.backends.values():lib.set_steps(16)

    def test_grid_matches_headunit_for_every_percentage_and_both_options(self):
        lib=self.backends['mock']
        for steps in [16,64]:
            positions=[int(i*100/steps+.5) for i in range(steps+1)]
            self.assertEqual(len(set(positions)),steps+1)
            for level in range(101):
                index=int(level*steps/100+.5)
                self.assertEqual(lib.snap(level,steps),positions[index])
                for delta in [-100,-2,-1,0,1,2,100]:
                    self.assertEqual(lib.step(level,delta,steps),positions[max(0,min(steps,index+delta))])
            for position in positions:self.assertEqual(lib.snap(position,steps),position)

    def test_off_grid_and_invalid_settings(self):
        lib=self.backends['mock']
        self.assertEqual(lib.snap(37,16),38)
        self.assertEqual(lib.step(37,1,16),44)
        self.assertEqual(lib.step(37,1,64),39)
        self.assertEqual(lib.snap(-10,16),0)
        self.assertEqual(lib.snap(110,64),100)
        self.assertEqual(lib.snap(37,0),38)
        lib.set_steps(64);lib.reset(37);lib.player_step_volume(1)
        self.assertEqual(lib.value(),39)

    def test_mpd_sends_one_native_step_per_detent(self):
        lib = self.backends['mpd']
        lib.reset(72)
        for direction in [1, 1, -1, -1, 1]:
            lib.player_step_volume(direction)
        self.assertEqual(lib.command_count(), 5)
        self.assertEqual([lib.command(i) for i in range(5)], [1, 1, -1, -1, 1])

    def test_mpd_steps_never_stop_at_a_local_estimated_limit(self):
        lib = self.backends['mpd']
        for estimate in [0, 72, 100]:
            lib.reset(estimate)
            for direction in [1] * 40 + [-1] * 40:
                lib.player_step_volume(direction)
            self.assertEqual(lib.command_count(), 80)
            self.assertEqual([lib.command(i) for i in range(80)], [1] * 40 + [-1] * 40)

    def test_zero_is_a_no_op_for_both_backends(self):
        for lib in self.backends.values():
            lib.reset(72)
            lib.player_step_volume(0)
            self.assertEqual(lib.value(), 72)
            self.assertEqual(lib.command_count(), 0)
            self.assertEqual(lib.revision(), 0)

    def test_mock_adjusts_in_both_directions_and_clamps(self):
        lib = self.backends['mock']
        lib.reset(72)
        lib.player_step_volume(1)
        self.assertEqual(lib.value(), 81)
        self.assertFalse(lib.is_muted())
        lib.player_step_volume(-1)
        self.assertEqual(lib.value(), 75)
        for _ in range(40):
            lib.player_step_volume(1)
        self.assertEqual(lib.value(), 100)
        for _ in range(40):
            lib.player_step_volume(-1)
        self.assertEqual(lib.value(), 0)
        self.assertEqual(lib.command_count(), 0)


if __name__ == '__main__':
    unittest.main()
