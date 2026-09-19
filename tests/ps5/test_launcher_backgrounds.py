"""Exercise the native package's launcher backgrounds and attribution."""
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / 'scripts/ps5/native-player'
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location('native_package', TOOLS / 'build.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class LauncherBackgrounds(unittest.TestCase):
    def test_native_backgrounds_are_packaged_with_attribution(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            runtime, media, sdl, graphics = [work / name for name in ('runtime', 'media', 'sdl', 'graphics')]
            for name in ('runtime/libc.prx', 'LICENSE'):
                path = runtime / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('fixture')
            for prefix in (media, sdl, graphics):
                (prefix / 'share/licenses').mkdir(parents=True)
            (media / 'share/native-media/fontconfig').mkdir(parents=True)
            (media / 'share/native-media/ca-bundle.crt').write_text('fixture')
            output = work / 'package'
            with mock.patch.object(package, 'digest', return_value=
                    'e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036'):
                package.package_resources(output, runtime, media, sdl, graphics)
            for name in ('pic0.dds', 'pic1.dds'):
                data = (output / 'sce_sys' / name).read_bytes()
                self.assertEqual(len(data), 148 + 3840 * 2160)
                self.assertEqual(data[:4], b'DDS ')
                self.assertEqual(struct.unpack_from('<II', data, 12), (2160, 3840))
                self.assertEqual(data[84:88], b'DX10')
                self.assertEqual(struct.unpack_from('<IIII', data, 128), (98, 3, 0, 1))
                self.assertEqual(data, (ROOT / 'app/platform/ps5/sce_sys/jellyfin-background.dds').read_bytes())
            notice = (output / 'licenses/Switchfin.txt').read_text()
            self.assertIn('pic0.dds', notice)
            self.assertIn('pic1.dds', notice)
            self.assertIn('CC BY-SA 4.0', notice)
            self.assertEqual((output / 'sce_sys/param.json').read_bytes(),
                             (ROOT / 'app/platform/ps5/param.json').read_bytes())


if __name__ == '__main__':
    unittest.main()
