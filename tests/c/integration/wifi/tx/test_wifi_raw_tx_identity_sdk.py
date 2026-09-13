"""Execute the pinned SDK allocation/complete call sites; inspect all target relocations."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tests.support.fixtures import fixture_text
from tests.c.integration.wifi.csi.test_wifi_csi_rx_link import tool, elf


class WiFiRawTxIdentitySdk(unittest.TestCase):
    def test_reviewed_call_relocations_and_sdk_execution(self):
        import patch_idf_raw_tx_identity as identity
        import patch_idf_raw_tx_management as management
        sdk = Path(os.environ.get('IDF_PATH', str(Path.home()/'esp/esp-idf')))
        for target in ('esp32c3', 'esp32s3', 'esp32c5'):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                ar, ld, cc = tool(target, 'ar'), tool(target, 'ld'), tool(target, 'gcc')
                archive = sdk/'components/esp_wifi/lib'/target/'libnet80211.a'
                if not ar or not ld or not cc or not archive.is_file():
                    self.skipTest('Pinned SDK and compiler required: '+target)
                pp = (archive.parent/'libpp.a').read_bytes()
                identity.verify_pp(pp, target)
                import patch_idf_twt_probe_wake as archive_tools
                changed_pp = archive_tools.patch_archive(pp, member_patches={'pp.o': lambda data: data[:-1]+bytes([data[-1]^1])})
                with self.assertRaises(ValueError): identity.verify_pp(changed_pp, target)
                for member in identity.HOOKS:
                    source = subprocess.check_output([ar, 'p', str(archive), member])
                    if member == 'ieee80211_output.o': source = management.patch_object(source, target)
                    changed = identity.patch_object(source, target, member)
                    sections, names, _ = elf(source)
                    after, _, _ = elf(changed)
                    # Every instruction and data section remains identical.
                    for i, name in enumerate(names):
                        if name.startswith(('.text', '.literal', '.rodata', '.data')):
                            self.assertEqual(source[sections[i][4]:sections[i][4]+sections[i][5]],
                                             changed[after[i][4]:after[i][4]+after[i][5]], name)
                    with self.assertRaises(ValueError): identity.patch_object(changed, target, member)
                    with self.assertRaises(ValueError): identity.patch_object(source[:-1], target, member)
                    (root/member).write_bytes(changed)
                    linked = subprocess.run([ld, '-EL', '-r', '--no-relax', str(root/member), '-o', str(root/'linked.o')], capture_output=True, text=True)
                    self.assertEqual(linked.returncode, 0, linked.stderr)
                if target == 'esp32s3': continue  # Xtensa target build proves final link; no host VM.
                qemu = shutil.which('qemu-riscv32')
                if not qemu: self.skipTest('qemu-riscv32 required')
                fixture = fixture_text('wifi/tx/test_wifi_raw_tx_management_sdk/fixture.inc')
                fixture = fixture.replace('u32 g_ic[512];', 'extern u32 g_ic[];')
                fixture = fixture.replace('void fixture_post_hmac_tx(void *buffer) {if(buffer==eb) posted++;}',
                    fixture_text('wifi/tx/test_wifi_raw_tx_identity_sdk/hooks.inc'))
                fixture = fixture.replace('    osi[21]=(void *)lock;',
                    '    g_ic['+str((0x1d0 if target=='esp32c5' else 0x1c0)//4)+']=(u32)completed;\n    osi[21]=(void *)lock;')
                fixture = fixture.replace('    return 0;\n}\n__attribute__',
                    '    CHECK(allocated_hooks==posted && completed_hooks==posted && public_callbacks==posted,90);\n    return 0;\n}\n__attribute__')
                (root/'fixture.c').write_text(fixture)
                built = subprocess.run([cc, '-Os', '-march=rv32imac', '-mabi=ilp32', '-mno-relax', '-nostdlib',
                    '-ffunction-sections', '-fdata-sections', '-fno-builtin', '-DEXPECT_EXTENDED=1',
                    '-DEB_TXINFO_WORD='+('14' if target=='esp32c5' else '11'), str(root/'fixture.c'),
                    str(root/'ieee80211_output.o'), str(root/'ieee80211.o'),
                    '-Wl,--gc-sections,-e,_start,--defsym=ieee80211_post_hmac_tx=fixture_post_hmac_tx',
                    '-o', str(root/'test.elf')], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([qemu, str(root/'test.elf')], capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
