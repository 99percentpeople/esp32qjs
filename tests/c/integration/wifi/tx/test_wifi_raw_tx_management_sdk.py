"""Real SDK management admission: subtype expansion preserves other constraints."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tests.support.fixtures import fixture_text
from tests.c.integration.wifi.csi.test_wifi_csi_rx_link import tool, elf


class WiFiRawTxManagementSdk(unittest.TestCase):
    def test_actual_sdk_management_admission_and_preserved_constraints(self):
        qemu=shutil.which('qemu-riscv32')
        if not qemu:
            self.skipTest('qemu-riscv32 required for actual SDK execution')
        sdk=Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/esp-idf')))
        for target in ('esp32c3','esp32c5'):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                cc,ar=tool(target,'gcc'),tool(target,'ar')
                archive=sdk/'components/esp_wifi/lib'/target/'libnet80211.a'
                if not cc or not ar or not archive.is_file():
                    self.skipTest('Pinned SDK and compiler required: '+target)
                root=Path(directory)
                original=subprocess.check_output([ar,'p',str(archive),'ieee80211_output.o'])
                import patch_idf_raw_tx_management as patcher
                for extended in (False,True):
                    with self.subTest(extendedManagement=extended):
                        (root/'sdk.o').write_bytes(patcher.patch_object(original,target) if extended else original)
                        (root/'fixture.c').write_text(fixture_text('wifi/tx/test_wifi_raw_tx_management_sdk/fixture.inc'))
                        command=[cc,'-Os','-march=rv32imac','-mabi=ilp32','-mno-relax','-nostdlib',
                            '-ffunction-sections','-fdata-sections','-fno-builtin','-DEXPECT_EXTENDED='+str(int(extended)),
                            '-DEB_TXINFO_WORD='+('14' if target=='esp32c5' else '11'),
                            str(root/'fixture.c'),str(root/'sdk.o'),'-Wl,--gc-sections,-e,_start,--defsym=ieee80211_post_hmac_tx=fixture_post_hmac_tx','-o',str(root/'test.elf')]
                        built=subprocess.run(command,capture_output=True,text=True,timeout=30)
                        self.assertEqual(built.returncode,0,built.stderr)
                        result=subprocess.run([qemu,str(root/'test.elf')],capture_output=True,text=True,timeout=10)
                        self.assertEqual(result.returncode,0,'Actual SDK assertion '+str(result.returncode)+' '+result.stderr)

    def test_three_target_instruction_scope_hash_guards_and_prior_fix_composition(self):
        import patch_idf_raw_tx_management as patcher
        import patch_idf_offchan_frame as offchan
        sdk=Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/esp-idf')))
        for target in ('esp32c3','esp32s3','esp32c5'):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                ar,ld,objdump=tool(target,'ar'),tool(target,'ld'),tool(target,'objdump')
                archive=sdk/'components/esp_wifi/lib'/target/'libnet80211.a'
                if not ar or not ld or not objdump or not archive.is_file():
                    self.skipTest('Pinned SDK and target tools required: '+target)
                original=subprocess.check_output([ar,'p',str(archive),'ieee80211_output.o'])
                prior=offchan.patch_object(original,target,'ieee80211_output.o')
                for source in (original,prior):
                    changed=patcher.patch_object(source,target)
                    sections,names,refs=elf(source)
                    index=names.index(patcher.FUNCTION)
                    offset,expected,replacement=patcher.EDITS[target]
                    start=sections[index][4]+offset
                    self.assertEqual(changed[:start],source[:start])
                    self.assertEqual(changed[start:start+len(expected)],replacement)
                    self.assertEqual(changed[start+len(expected):],source[start+len(expected):])
                    self.assertEqual(elf(changed)[2],refs)
                    with self.assertRaises(ValueError):
                        patcher.patch_object(changed,target)
                    with self.assertRaises(ValueError):
                        patcher.patch_object(source[:-1]+bytes([source[-1]^1]),target)
                    root=Path(directory);(root/'sdk.o').write_bytes(changed)
                    linked=subprocess.run([ld,'-EL','-r','--no-relax',str(root/'sdk.o'),
                        '-o',str(root/'linked.o')],capture_output=True,text=True,timeout=30)
                    self.assertEqual(linked.returncode,0,linked.stderr)
                    assembly=subprocess.check_output([objdump,'-dr',
                        '--disassemble=ieee80211_raw_frame_sanity_check',str(root/'linked.o')],text=True)
                    self.assertRegex(assembly, r'beq\s+a7, a7' if target=='esp32s3' else r'beqz\s+zero,')
                # The archive writer must retain every other member verbatim.
                source_archive=archive.read_bytes()
                output=patcher.patch_archive(source_archive,target)
                (Path(directory)/'archive.a').write_bytes(output)
                members=subprocess.check_output([ar,'t',str(archive)],text=True).splitlines()
                for member in members:
                    actual=subprocess.check_output([ar,'p',str(Path(directory)/'archive.a'),member])
                    expected=subprocess.check_output([ar,'p',str(archive),member])
                    if member=='ieee80211_output.o': expected=patcher.patch_object(expected,target)
                    self.assertEqual(actual,expected,member)
