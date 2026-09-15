"""Managed camera sources stay untouched; drift is rejected before output writes."""
import hashlib
import os
from pathlib import Path
import tempfile
import unittest

from tests.support.paths import ROOT
from sdk_patches.camera.esp32_camera import INPUTS, prepare


class CameraSdkPatch(unittest.TestCase):
    def test_drift_and_in_place_output_are_rejected_without_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            component = Path(directory) / 'component'
            component.mkdir()
            (component / 'idf_component.yml').write_text('version: 2.1.8\n')
            output = Path(directory) / 'output'
            with self.assertRaisesRegex(ValueError, 'Unreviewed'):
                prepare(component, output)
            self.assertFalse(output.exists())
            with self.assertRaisesRegex(ValueError, 'outside'):
                prepare(component, component / 'generated')
            self.assertEqual((component / 'idf_component.yml').read_text(), 'version: 2.1.8\n')

    def test_locked_component_produces_exact_outputs_without_modifying_inputs(self):
        component = Path(os.environ.get('ESP32QJS_CAMERA_COMPONENT_DIR',
                                       str(ROOT / 'managed_components/espressif__esp32-camera')))
        if not component.is_dir():
            self.skipTest('Locked camera component unavailable')
        before = {name: (component / name).read_bytes() for name in INPUTS['inputs']}
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            prepare(component, output)
            times = {}
            for name, digest in INPUTS['outputs'].items():
                self.assertEqual(hashlib.sha256((output / name).read_bytes()).hexdigest(), digest)
                times[name] = (output / name).stat().st_mtime_ns
            prepare(component, output)
            self.assertEqual(times, {name: (output / name).stat().st_mtime_ns for name in times})
        self.assertEqual(before, {name: (component / name).read_bytes() for name in before})
