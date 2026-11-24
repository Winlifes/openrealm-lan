"""Release freezing must use the selected compiler output, never stale preview files."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest


class BrowserReleaseInputs(unittest.TestCase):
    def test_selected_engine_output_replaces_stale_preview(self):
        script = Path(__file__).with_name('stage-browser-release.py')
        with tempfile.TemporaryDirectory() as home:
            root = Path(home) / '.local/share/wc3-lan'
            preview = root / 'development/browser-preview'
            compiled = Path(home) / 'selected-build/bin'
            preview.mkdir(parents=True)
            compiled.mkdir(parents=True)
            files = ['index.html', 'wc3-input.js', 'wc3-movies.js', 'assets.index.txt',
                     'playable-maps.index.txt', 'bootstrap.mpq']
            for name in files:
                (preview / name).write_bytes(name.encode())
            (preview / 'playable-maps.index.txt').write_text('maps/test.w3m\n')
            (preview / 'bootstrap-manifest.json').write_text(json.dumps({
                'sha256': hashlib.sha256((preview / 'bootstrap.mpq').read_bytes()).hexdigest(),
                'bytes': (preview / 'bootstrap.mpq').stat().st_size,
            }))
            binaries = ['openwarcraft3.js', 'openwarcraft3.wasm', 'openwarcraft3.data']
            for name in binaries:
                (preview / name).write_bytes(b'stale preview')
                (compiled / name).write_bytes(('current compiled ' + name).encode())
            (root / 'www/play').mkdir(parents=True)
            served_index = Path(home) / 'served-assets.txt'
            served_index.write_text('maps/test.w3m\nui/frame.dds\n')
            preload_plan = {'lobby': [], 'maps': {'maps/test.w3m': ['ui/frame.dds']}}
            preload_index = Path(home) / 'preload.index.json'
            preload_index.write_text(json.dumps(preload_plan))
            result = subprocess.run([sys.executable, str(script), 'test-build',
                                     '--engine-build', str(compiled), '--asset-index', str(served_index),
                                     '--preload-index', str(preload_index)],
                                    env={**os.environ, 'WC3_LAN_ROOT': str(root)},
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            release = root / 'browser-releases/test-build'
            manifest = json.loads((release / 'release-manifest.json').read_text())
            self.assertEqual((release / 'assets.index.txt').read_bytes(), served_index.read_bytes())
            self.assertEqual(json.loads((release / 'preload.index.json').read_text()), preload_plan)
            self.assertIn('fullscreen-resize.js', manifest)
            self.assertIn('early-map-loading.js', manifest)
            for name in binaries:
                body = (compiled / name).read_bytes()
                self.assertEqual((release / name).read_bytes(), body, name)
                self.assertEqual(manifest[name]['sha256'], hashlib.sha256(body).hexdigest())


if __name__ == '__main__':
    unittest.main()
