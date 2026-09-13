"""Execute the pinned SDK rate validator, including its stopped-driver path."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from tests.c.integration.wifi.csi.test_wifi_csi_rx_link import tool, elf
from tests.c.integration.wifi.tx.test_wifi_tx_rate import rate_code

ROOT = TEST_ROOT
sys.path.insert(0, str(ROOT / 'scripts'))

FIXTURE = fixture_text('wifi/tx/test_wifi_tx_rate_sdk/fixture.inc')


class WiFiTxRateSdk(unittest.TestCase):
    def test_patch_scope_hash_guard_and_three_target_link(self):
        import patch_idf_tx_rate as patcher
        sdk = Path(os.environ.get('IDF_PATH', str(Path.home() / 'esp/esp-idf')))
        for target in ('esp32c3', 'esp32s3', 'esp32c5'):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                ar, ld = tool(target, 'ar'), tool(target, 'ld')
                archive = sdk/'components/esp_wifi/lib'/target/'libnet80211.a'
                if not ar or not ld or not archive.exists():
                    self.skipTest('Pinned SDK and target linker required: '+target)
                source = subprocess.check_output([ar,'p',str(archive),'ieee80211_api.o'])
                changed = patcher.patch_object(source,target)
                with self.assertRaises(ValueError):
                    patcher.patch_object(changed,target)
                with self.assertRaises(ValueError):
                    patcher.patch_object(source[:-1]+bytes([source[-1]^1]),target)
                sections,names,refs = elf(source)
                new_sections,new_names,new_refs = elf(changed)
                edits = patcher.XTENSA_EDITS if target=='esp32s3' else patcher.RISCV_EDITS
                for section,name in zip(sections,names):
                    if not section[2]&4:
                        continue
                    expected = bytearray(source[section[4]:section[4]+section[5]])
                    if name == patcher.FUNCTION:
                        for offset,value in edits.items():
                            expected[offset:offset+len(value)] = value
                    new = new_sections[new_names.index(name)]
                    self.assertEqual(changed[new[4]:new[4]+new[5]],expected,name)
                for key,value in refs.items():
                    section,offset,kind = key
                    touched = section == patcher.FUNCTION and (
                        (offset==0x14 or 0x9d<=offset<0xb9) if target=='esp32s3'
                        else 0x6e<=offset<0x8c)
                    if not touched:
                        self.assertEqual(new_refs[key],value,str(key))
                root = Path(directory)
                (root/'sdk.o').write_bytes(changed)
                result = subprocess.run([ld,'-EL','-r','--no-relax',str(root/'sdk.o'),
                    '-o',str(root/'linked.o')],capture_output=True,text=True,timeout=30)
                self.assertEqual(result.returncode,0,result.stderr)
                _,_,linked_refs = elf((root/'linked.o').read_bytes())
                site,kind = (0x14,1) if target=='esp32s3' else (0x72,18)
                self.assertEqual(linked_refs[patcher.FUNCTION,site,kind][0][0],patcher.HELPER)

    def test_actual_sdk_validator_before_start(self):
        qemu = shutil.which('qemu-riscv32')
        if not qemu:
            self.skipTest('qemu-riscv32 is required to execute the real SDK validator')
        sdk = Path(os.environ.get('IDF_PATH', str(Path.home() / 'esp/esp-idf')))
        for target in ('esp32c3', 'esp32c5'):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                cc, ar = tool(target, 'gcc'), tool(target, 'ar')
                archive = sdk / 'components/esp_wifi/lib' / target / 'libnet80211.a'
                if not cc or not ar or not archive.exists():
                    self.skipTest('Pinned SDK and compiler required: '+target)
                root = Path(directory)
                original = subprocess.check_output([ar, 'p', str(archive), 'ieee80211_api.o'])
                import patch_idf_tx_rate
                original = patch_idf_tx_rate.patch_object(original, target)
                (root/'sdk.o').write_bytes(original)
                # Reuse inventory-derived SDK typedefs, before the production
                # framework header/source that rate_code also appends.
                header = rate_code(target+'/representative').split('#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI', 1)[0]
                (root/'esp_wifi.h').write_text('#pragma once\n'+header+fixture_text('wifi/tx/test_wifi_tx_rate_sdk/test_actual_sdk_validator_before_start.inc'))
                (root/'sdkconfig.h').write_text('#define CONFIG_IDF_TARGET_'+target.upper()+' 1\n')
                (root/'fixture.c').write_text(FIXTURE)
                helper = ROOT/'components/esp32_mquickjs/src/modules/wifi_driver/esp32_mquickjs_wifi_tx_rate_sdk.c'
                command = [cc,'-Os','-march=rv32imac','-mabi=ilp32','-mno-relax','-nostdlib',
                    '-ffunction-sections','-fdata-sections','-fno-builtin','-I'+str(root),
                    '-DPHY_OFFSET='+('348' if target=='esp32c5' else '340'),
                    '-DSECONDARY_OFFSET='+('85' if target=='esp32c5' else '81'),
                    str(root/'fixture.c'),str(root/'sdk.o')]
                command += [str(helper)]
                command += ['-Wl,--gc-sections,--defsym=wifi_init_completed=fixture_initialized',
                    '-Wl,--defsym=esp_wifi_get_protocol=fixture_get_protocol',
                    '-Wl,-e,_start','-o',str(root/'test.elf')]
                if target == 'esp32c5':
                    command += ['-Wl,--defsym=esp_wifi_get_protocols=fixture_get_protocols']
                result = subprocess.run(command,text=True,capture_output=True,timeout=30)
                self.assertEqual(result.returncode,0,result.stderr)
                result = subprocess.run([qemu,str(root/'test.elf')],capture_output=True,text=True,timeout=10)
                self.assertEqual(result.returncode,0,'Actual '+target+' SDK assertion '+str(result.returncode)+' '+result.stderr)
