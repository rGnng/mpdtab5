"""Both hardware projects must compile the canonical shared controller headers."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = '#include "coloplay_ble_protocol.h"\n#include "volume_grid.h"\n#include "coloplay_usb_frame.h"\n'


class SharedControllerTests(unittest.TestCase):
    def test_both_firmware_include_paths_resolve_to_the_same_definitions(self):
        for compiler, language in [('cc', 'c'), ('c++', 'c++')]:
            if not shutil.which(compiler):
                self.skipTest(compiler + ' is unavailable')
            with self.subTest(language=language):
                outputs = []
                for include_paths in [('firmware/main', 'app/backend'), ('coloplay/firmware/include',)]:
                    command = [compiler, '-x', language]
                    command += ['-I' + str(ROOT / path) for path in include_paths]
                    outputs.append(subprocess.run(command + ['-E', '-P', '-'], input=SOURCE,
                                                  text=True, capture_output=True, check=True).stdout)
                    dependencies = subprocess.run(command + ['-M', '-'], input=SOURCE,
                                                  text=True, capture_output=True, check=True).stdout
                    for name in ('coloplay_ble_protocol.h', 'volume_grid.h', 'volume_scale_tables.h', 'coloplay_usb_frame.h'):
                        self.assertIn('shared/' + name, dependencies)
                self.assertEqual(outputs[0], outputs[1])


if __name__ == '__main__':
    unittest.main()
