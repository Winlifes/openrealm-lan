"""Release freezing must use the selected compiler output, never stale preview files."""
from pathlib import Path
import gzip
import hashlib
import json
import os
import subprocess
import struct
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
            self.assertEqual(json.loads((release / 'version.json').read_text()),
                             {'build': 'test-build', 'release': 'test-build'})
            manifest = json.loads((release / 'release-manifest.json').read_text())
            self.assertEqual((release / 'assets.index.txt').read_bytes(), served_index.read_bytes())
            self.assertEqual(json.loads((release / 'preload.index.json').read_text()), preload_plan)
            self.assertIn('fullscreen-resize.js', manifest)
            self.assertIn('early-map-loading.js', manifest)
            self.assertIn('wc3-chat-input.js', manifest)
            self.assertIn('wc3-room-assets.js', manifest)
            for name in binaries:
                body = (compiled / name).read_bytes()
                self.assertEqual((release / name).read_bytes(), body, name)
                self.assertEqual(manifest[name]['sha256'], hashlib.sha256(body).hexdigest())
            result = subprocess.run([sys.executable, str(script), 'updated-browser',
                                     '--server-build', 'test-build', '--engine-build', str(compiled),
                                     '--asset-index', str(served_index), '--preload-index', str(preload_index)],
                                    env={**os.environ, 'WC3_LAN_ROOT': str(root)},
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            version = root / 'browser-releases/updated-browser/version.json'
            self.assertEqual(json.loads(version.read_text()), {'build': 'test-build', 'release': 'updated-browser'})
            native = root / 'engine-releases/test-build/bin/openwarcraft3'
            native.parent.mkdir(parents=True)
            native.touch()
            result = subprocess.run([sys.executable, str(script.with_name('stage-lan-site.py')),
                                     'updated-browser', '--root', str(root)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('../releases/updated-browser/', (root / 'www/play/game/index.html').read_text())
            self.assertEqual((root / 'www/play/game/version.json').read_bytes(), version.read_bytes())
            self.assertTrue((root / 'www/release-notes.json').is_file())

            data = root / 'data-current'
            archive = b'map archive'
            model = b'compressed model ' * 64
            resources = {'maps/test.w3m': archive, 'ui/frame.dds': b'shared', 'units/model.dds': model,
                         'units/test/unit.mdx': b'unit model', 'units/test/attack.ogg': b'attack clip',
                         'units/unused/attack.ogg': b'unrelated clip', 'sound/interface/click.wav': b'UI clip'}
            for path, body in resources.items():
                file = data / path
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(body)
            served_index.write_text('\n'.join([*resources, 'units/model.blp']) + '\n')
            preload_index.write_text(json.dumps({'lobby':['ui/frame.dds'],
                'maps':{'maps/test.w3m':['ui/frame.dds', 'units/model.blp', 'units/test/unit.mdx']},
                'retainAfterLoad':{'maps/test.w3m':['ui/frame.dds']}}))
            command = [sys.executable, str(script), 'with-packs', '--engine-build', str(compiled),
                       '--asset-index', str(served_index), '--preload-index', str(preload_index), '--data-dir', str(data)]
            env = {**os.environ, 'WC3_LAN_ROOT': str(root)}
            target = root / 'browser-releases/with-packs'
            (data / 'units/model.dds').unlink()
            result = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(target.exists())
            (data / 'units/model.dds').write_bytes(model)
            result = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            plan = json.loads((target / 'preload.index.json').read_text())
            self.assertEqual(plan['maps']['maps/test.w3m'], ['units/model.blp', 'units/test/unit.mdx'])
            self.assertEqual(plan['retainAfterLoad']['maps/test.w3m'], ['ui/frame.dds'])
            self.assertEqual(plan['archives']['maps/test.w3m'],
                             {'bytes':len(archive), 'sha256':hashlib.sha256(archive).hexdigest()})
            self.assertEqual(list(json.loads((target / 'room-maps.index.json').read_text()).values()), ['maps/test.w3m'])
            packed = (target / plan['packs']['maps']['maps/test.w3m']).read_bytes()
            size, decoded_size = struct.unpack('<II', packed[:8])
            self.assertEqual(len(packed) - 8 - size, 8 + len(resources['units/test/unit.mdx']))
            self.assertEqual(decoded_size, len(model))
            self.assertEqual(gzip.decompress(packed[8:8 + size]), model)

            shared = plan['audio']['shared']
            selected = plan['audio']['maps']['maps/test.w3m']
            self.assertEqual(shared['files'], ['sound/interface/click.wav'])
            self.assertEqual(selected['files'], ['units/test/attack.ogg'])
            manifest = json.loads((target / 'release-manifest.json').read_text())
            for group in (shared, selected):
                body = (target / group['pack']).read_bytes()
                self.assertEqual(body[8:], resources[group['files'][0]])
                self.assertEqual(manifest[group['pack']]['sha256'], hashlib.sha256(body).hexdigest())


if __name__ == '__main__':
    unittest.main()
