"""Deferred exact SDK archive transformation checks; no fixture execution this wave."""
from tests.support.paths import ROOT as TEST_ROOT
import importlib.util
from pathlib import Path
import unittest
import sys
import struct
import subprocess
import tempfile
from unittest.mock import patch as mock_patch

ROOT = TEST_ROOT


def load_patch(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts/patch_idf_vendor_ie_context.py')
    module = importlib.util.module_from_spec(spec)
    with mock_patch.object(sys, 'path', [str(ROOT / 'scripts'), *sys.path]):
        spec.loader.exec_module(module)
    return module


class VendorIeContextPatch(unittest.TestCase):
    def test_probe_pm_only_retargets_reviewed_calls(self):
        patch = load_patch('probe_wake_patch')
        source_path = Path('/home/zach/esp/esp-idf/components/esp_wifi/lib/esp32c5/libnet80211.a')
        prefix = '/home/zach/.espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin/riscv32-esp-elf-'
        if not source_path.exists() or not Path(prefix + 'ar').exists():
            self.skipTest('pinned C5 SDK/toolchain unavailable')
        source = source_path.read_bytes()
        baseline = patch.patch_archive(source, 'esp32c5', ftm_report_null_fix=True, twt_probe_buffer_fix=True)
        fixed = patch.patch_archive(source, 'esp32c5', ftm_report_null_fix=True,
                                    twt_probe_buffer_fix=True, twt_probe_wake_fix=True)
        selected = {'ieee80211_ioctl.o', 'ieee80211_twt.o', 'ieee80211_sta.o', 'ieee80211_btwt.o'}
        sites = {
            'ieee80211_ioctl.o': [('wifi_sta_itwt_send_probe_req_process', '4a', 'pm_wake_up', 'up'),
                                ('wifi_sta_itwt_send_probe_req_process', '9c', 'pm_wake_done', 'done')],
            'ieee80211_twt.o': [('ieee80211_itwt_information', '160', 'pm_twt_wake_up', 'esp32_mquickjs_wifi_twt_information_wake_up_native'),
                              ('ieee80211_itwt_teardown', '58', 'pm_twt_wake_up', 'esp32_mquickjs_wifi_twt_teardown_wake_up_native'),
                              ('he_twt_teardown_txcb', '28', 'pm_twt_wake_done', 'esp32_mquickjs_wifi_twt_teardown_wake_done_native'),
                              ('itwt_stop_process', '6e', 'pm_wake_done', 'done'),
                              ('itwt_probe_timeout_fn_process', '9a', 'pm_wake_done', 'done'),
                              ('itwt_probe_rc_tx_cb', 'ea', 'pm_wake_done', 'done')],
            'ieee80211_btwt.o': [('ieee80211_btwt_teardown', '5a', 'pm_twt_wake_up', 'esp32_mquickjs_wifi_twt_teardown_wake_up_native')],
            'ieee80211_sta.o': [('sta_recv_mgmt', '2cc', 'pm_wake_done', 'done')],
        }
        with tempfile.TemporaryDirectory() as temp:
            before = Path(temp) / 'before.a'
            after = Path(temp) / 'after.a'
            before.write_bytes(baseline)
            after.write_bytes(fixed)
            members = subprocess.check_output([prefix + 'ar', 't', str(before)])
            self.assertEqual(members, subprocess.check_output([prefix + 'ar', 't', str(after)]))
            self.assertEqual(subprocess.check_output([prefix + 'nm', '-s', '--defined-only', str(before)], stderr=subprocess.DEVNULL),
                             subprocess.check_output([prefix + 'nm', '-s', '--defined-only', str(after)], stderr=subprocess.DEVNULL))
            for member in members.decode().splitlines():
                old = subprocess.check_output([prefix + 'ar', 'p', str(before), member])
                new = subprocess.check_output([prefix + 'ar', 'p', str(after), member])
                if member not in selected:
                    self.assertEqual(old, new)
                    continue
                obj = Path(temp) / member
                obj.write_bytes(old)
                old_dis = subprocess.check_output([prefix + 'objdump', '-dr', str(obj)]).decode()
                obj.write_bytes(new)
                new_dis = subprocess.check_output([prefix + 'objdump', '-dr', str(obj)]).decode()
                for function, offset, original, target in sites[member]:
                    start = old_dis.index('Disassembly of section .text.' + function + ':')
                    end = old_dis.find('Disassembly of section ', start + 1)
                    if end < 0:
                        end = len(old_dis)
                    section = old_dis[start:end]
                    original_call = offset + ': R_RISCV_CALL\t' + original
                    replacement = offset + ': R_RISCV_CALL\t' + (target if target.startswith('esp32_') else 'esp32_mquickjs_wifi_twt_probe_wake_' + target + '_native')
                    self.assertEqual(section.count(original_call), 1)
                    old_dis = old_dis[:start] + section.replace(original_call, replacement) + old_dis[end:]
                self.assertEqual(old_dis, new_dis)
                # ELF allocatable code/data sections keep their offsets/bytes;
                # the source-gated repair changes only linking metadata.
                table = struct.unpack_from('<I', old, 32)[0]
                count = struct.unpack_from('<H', old, 48)[0]
                self.assertEqual(struct.unpack_from('<I', new, 32)[0], table)
                for i in range(count):
                    section = struct.unpack_from('<10I', old, table + i * 40)
                    if section[2] & 2:
                        self.assertEqual(old[table + i * 40:table + (i + 1) * 40], new[table + i * 40:table + (i + 1) * 40])
                        if section[1] != 8:  # NOBITS has no file contents.
                            start, size = section[4:6]
                            self.assertEqual(old[start:start + size], new[start:start + size])
        with self.assertRaises(ValueError):
            patch.patch_archive(fixed, 'esp32c5', twt_probe_wake_fix=True)

    def test_twt_probe_callback_preserves_every_other_archive_byte(self):
        patch = load_patch('twt_probe_patch')
        path = Path('/home/zach/esp/esp-idf/components/esp_wifi/lib/esp32c5/libnet80211.a')
        if not path.exists():
            self.skipTest('pinned local SDK archive unavailable')
        source = path.read_bytes()
        baseline = patch.patch_archive(source, 'esp32c5', ftm_report_null_fix=True)
        result = patch.patch_archive(source, 'esp32c5', ftm_report_null_fix=True, twt_probe_buffer_fix=True)
        offset = source.index(patch.C5_PROBE_CALLBACK) + 0x38
        self.assertEqual(baseline[offset:offset + 4], bytes.fromhex('03c53701'))
        self.assertEqual(result[offset:offset + 4], bytes.fromhex('13050400'))
        self.assertEqual(len(result), len(baseline))
        self.assertEqual(result[:offset], baseline[:offset])
        self.assertEqual(result[offset + 4:], baseline[offset + 4:])
        with self.assertRaises(ValueError):
            patch.patch_archive(result, 'esp32c5', twt_probe_buffer_fix=True)

    def test_exact_inputs_fixed_load_and_no_other_archive_changes(self):
        patch = load_patch('vendor_context_patch')
        sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/lib')
        for target in patch.REVIEWED:
            with self.subTest(target=target):
                path = sdk / target / 'libnet80211.a'
                if not path.exists():
                    self.skipTest('pinned local SDK archive unavailable')
                source = path.read_bytes()
                result = patch.patch_archive(source, target)
                before = patch.XTENSA_FUNCTION if target == 'esp32s3' else patch.RISCV_FUNCTION
                index = source.index(before)
                offset, width = (12, 3) if target == 'esp32s3' else (10, 2)
                self.assertEqual(len(result), len(source))
                expected = bytearray(source)
                expected[index + offset:index + offset + width] = bytes.fromhex('222205' if target == 'esp32s3' else '4849')
                for function, relative in patch.RESTORE_FUNCTIONS[target]:
                    at = source.index(function) + relative
                    expected[at:at + 2] = bytes.fromhex('2d0a' if target == 'esp32s3' else '0100')
                self.assertEqual(result, expected)
                for invalid in (result, source[:-1], bytes([source[0] ^ 1]) + source[1:]):
                    with self.assertRaises(ValueError):
                        patch.patch_archive(invalid, target)
                with self.assertRaises(ValueError):
                    patch.patch_archive(source, 'unreviewed-target')

    def test_ftm_null_discard_preserves_archive_size_and_branch_relocations(self):
        patch = load_patch('ftm_report_patch')
        sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/lib')
        for target in patch.REVIEWED:
            with self.subTest(target=target):
                path = sdk / target / 'libnet80211.a'
                if not path.exists():
                    self.skipTest('pinned local SDK archive unavailable')
                source = path.read_bytes()
                baseline = patch.patch_archive(source, target)
                fixed = patch.patch_archive(source, target, ftm_report_null_fix=True)
                section = patch.FTM_REPORT_FUNCTIONS[target]
                position = source.index(section)
                offset, original, replacement = ((0x3b, bytes.fromhex('9cd2'), bytes.fromhex('9cd8'))
                    if target == 'esp32s3' else (0x28, bytes.fromhex('15c0'), bytes.fromhex('15c1')))
                begin = position + offset
                self.assertEqual(baseline[begin:begin + 2], original)
                self.assertEqual(fixed[begin:begin + 2], replacement)
                self.assertEqual(len(fixed), len(baseline))
                # All relocation tables, guards, state checks and native IPC are
                # outside this two-byte instruction and remain byte-identical.
                self.assertEqual(fixed[:begin], baseline[:begin])
                self.assertEqual(fixed[begin + 2:], baseline[begin + 2:])
                with self.assertRaises(ValueError):
                    patch.patch_archive(fixed, target, ftm_report_null_fix=True)
