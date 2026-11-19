"""Boundary checks for generated names and include guards."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('FDFBINDGEN', ROOT / 'build/bin/fdfbindgen'))


class FdfBindingNames(unittest.TestCase):
    def test_flattened_variant_containers_keep_common_children_required(self):
        sources = [
            'Frame "FRAME" "Root" { Layer "Artwork" { Texture "Queue" {} } String "Title" {} }',
            'Frame "FRAME" "Root" { Frame "FRAME" "QueueGroup" { Texture "Queue" {} } String "Title" {} }',
        ]
        with tempfile.TemporaryDirectory() as directory:
            files = []
            for i, text in enumerate(sources):
                path = Path(directory, f'variant{i}.fdf')
                path.write_text(text)
                files.append(str(path))
            result = subprocess.run([str(BINARY), '-prefix', 'Example',
                                     '-root', 'Root', '-flatten', 'Artwork', '-flatten', 'QueueGroup', *files],
                                    text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('BZ_FDF_BIND_CHILD(out, Queue, bind_root, "Queue");', result.stdout)
        self.assertIn('BZ_FDF_BIND_CHILD(out, Title, bind_root, "Title");', result.stdout)
        self.assertNotIn('out->Artwork', result.stdout)
        self.assertNotIn('out->QueueGroup', result.stdout)
        self.assertNotIn('Queue_2', result.stdout)

    def test_variant_only_control_does_not_disable_common_controls(self):
        with tempfile.TemporaryDirectory() as directory:
            files = []
            for i, extra in enumerate(['Frame "BACKDROP" "Notch" { String "Label" {} }', '']):
                path = Path(directory, f'variant{i}.fdf')
                path.write_text('Frame "FRAME" "Root" { Frame "SLIDER" "Slider" {} ' + extra + '}')
                files.append(str(path))
            result = subprocess.run([str(BINARY), '-prefix', 'Example', '-root', 'Root', *files],
                                    text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('BZ_FDF_BIND_CHILD(out, Slider, bind_root, "Slider");', result.stdout)
        self.assertIn('BZ_FDF_BIND_CHILD_OPTIONAL(out, Notch, bind_root, "Notch");', result.stdout)
        self.assertIn('BZ_FDF_BIND_CHILD_OPTIONAL(out, Label, out->Notch, "Label");', result.stdout)

    def test_include_guard_keeps_suffix(self):
        for prefix in ['Example', 'a' * 159]:
            result = subprocess.run([str(BINARY), '-prefix', prefix, '-'],
                                    input='Frame "FRAME" "Root" {}', text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('#ifndef ' + prefix.upper() + '_H\n', result.stdout)

    def test_duplicate_name_overflow_is_an_error(self):
        source = r'''
#define main fdfbindgen_main
#include "tools/fdfbindgen.c"
#undef main
int main(int argc, char **argv) {
    node_count = 2;
    nodes[0].parent = nodes[1].parent = -1;
    nodes[0].first_child = nodes[1].first_child = -1;
    memset(nodes[0].ident, 'a', MAX_IDENT - 1);
    strcpy(nodes[1].ident, nodes[0].ident);
    if (argc > 1) {
        strcpy(nodes[0].binding_ident, nodes[0].ident);
        assign_binding_idents(1);
    } else uniquify_node_ident(1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory, 'names'))
            subprocess.run(['cc', '-I', str(ROOT), '-x', 'c', '-', '-o', binary],
                           input=source, text=True, check=True)
            for args in [[], ['binding']]:
                result = subprocess.run([binary, *args], capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 1)
                self.assertIn('identifier too long', result.stderr)


if __name__ == '__main__':
    unittest.main()
