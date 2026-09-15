"""Deferred exact pinned NAN archive transformation; does not link or run RF."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.idf import require_idf
import importlib.util
import struct
import sys
import unittest
from pathlib import Path

ROOT = TEST_ROOT


def members(archive):
    result, names, pos = {}, b'', 8
    while pos < len(archive):
        header = archive[pos:pos + 60]
        size = int(header[48:58])
        data = archive[pos + 60:pos + 60 + size]
        name = header[:16].decode().strip()
        if name == '//':
            names = data
        elif name.startswith('/') and name[1:].isdigit():
            index = int(name[1:])
            result[names[index:names.index(b'/\n', index)].decode()] = data
        elif not name.startswith('/'):
            result[name.rstrip('/')] = data
        pos += 60 + size + (size & 1)
    return result


def allocated_sections(obj):
    start = struct.unpack_from('<I', obj, 32)[0]
    width, count = struct.unpack_from('<HH', obj, 46)
    result = []
    for i in range(count):
        section = struct.unpack_from('<10I', obj, start + width * i)
        if section[2] & 2:
            result.append((i, section, obj[section[4]:section[4] + section[5]]))
    return result


class NanSDArchive(unittest.TestCase):
    def test_production_patch_keeps_code_and_other_members_and_rejects_other_targets(self):
        source = require_idf() / 'components/esp_wifi/lib/esp32c5/libnet80211.a'
        if not source.exists():
            self.skipTest('Reviewed SDK unavailable')
        sys.path.insert(0, str(ROOT / 'scripts'))
        self.addCleanup(lambda: sys.path.remove(str(ROOT / 'scripts')))
        spec = importlib.util.spec_from_file_location('nan_sd_patch', ROOT / 'scripts/sdk_patches/wifi/nan_sd.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        original = source.read_bytes()
        prepared = patch.patch_archive(original, 'esp32c5')
        before, after = members(original), members(prepared)
        self.assertEqual(before.keys(), after.keys())
        for name in before:
            if name in ('ieee80211_nan_sd.o', 'ieee80211_nan_datapath.o'):
                self.assertEqual(allocated_sections(before[name]), allocated_sections(after[name]))
                if name == 'ieee80211_nan_sd.o':
                    self.assertIn(b'esp32qjs_wifi_nan_alloc_sdf\0', after[name])
                else:
                    for boundary in ('esp32qjs_wifi_nan_alloc_action', 'esp32qjs_nan_ndp_alloc',
                                     'esp32qjs_nan_ndp_delete', 'esp32qjs_nan_ndp_peer_service',
                                     'esp32qjs_nan_setup_timer_callback', 'esp32qjs_nan_inactivity_timer_callback'):
                        self.assertIn(boundary.encode() + b'\0', after[name])
            else:
                self.assertEqual(before[name], after[name], name)
        with self.assertRaises(ValueError):
            patch.patch_archive(prepared, 'esp32c5')
        for target in ('esp32c3', 'esp32s3'):
            with self.assertRaises(ValueError):
                patch.patch_archive(original, target)
        bad = bytearray(before['ieee80211_nan_sd.o'])
        bad[-1] ^= 1
        with self.assertRaises(ValueError):
            patch.patch_object(bytes(bad), 'ieee80211_nan_sd.o')
