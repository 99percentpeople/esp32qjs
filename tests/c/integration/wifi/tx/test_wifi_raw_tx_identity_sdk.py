"""Execute the pinned SDK allocation/complete call sites; inspect all target relocations."""
import os
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tests.support.fixtures import fixture_text
from tests.c.integration.wifi.csi.test_wifi_csi_rx_link import tool, elf
from tests.support.wireless_vm_fixture import extract


class WiFiRawTxIdentitySdk(unittest.TestCase):
    def test_reviewed_call_relocations_and_sdk_execution(self):
        self.run_sdk_cases(('esp32c3', 'esp32s3', 'esp32c5'))

    def test_reported_c5_frames_sequence_choices_and_actual_metadata_getter(self):
        self.run_sdk_cases(('esp32c5',), metadata=True)

    def test_c5_static_tx_cache_moves_descriptor_before_completion(self):
        self.run_sdk_cases(('esp32c5',), metadata=True, cache=True)

    def run_sdk_cases(self, targets, metadata=False, cache=False):
        import patch_idf_raw_tx_identity as identity
        import patch_idf_raw_tx_management as management
        sdk = Path(os.environ.get('IDF_PATH', str(Path.home()/'esp/esp-idf')))
        for target in targets:
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                ar, ld, cc = tool(target, 'ar'), tool(target, 'ld'), tool(target, 'gcc')
                archive = sdk/'components/esp_wifi/lib'/target/'libnet80211.a'
                if not ar or not ld or not cc or not archive.is_file():
                    self.skipTest('Pinned SDK and compiler required: '+target)
                pp = (archive.parent/'libpp.a').read_bytes()
                identity.verify_pp(pp, target)
                import patch_idf_twt_probe_wake as archive_tools
                for member in ('pp.o', 'lmac.o'):
                    changed_pp = archive_tools.patch_archive(pp, member_patches={member: lambda data: data[:-1]+bytes([data[-1]^1])})
                    with self.assertRaises(ValueError): identity.verify_pp(changed_pp, target)
                for member in identity.HOOKS:
                    source = subprocess.check_output([ar, 'p', str(archive), member])
                    if member == 'ieee80211_output.o': source = management.patch_object(source, target)
                    if member == 'ieee80211_api.o':
                        import patch_idf_tx_rate as rate
                        # Preserve the predecessor patch used in target builds.
                        source = rate.patch_object(source, target)
                    changed = identity.patch_object(source, target, member)
                    sections, names, _ = elf(source)
                    after, _, _ = elf(changed)
                    # Every instruction and data section remains identical.
                    for i, name in enumerate(names):
                        if sections[i][2] & 4 or name.startswith(('.text', '.literal', '.rodata', '.data')):
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
                    fixture_text('wifi/tx/test_wifi_raw_tx_identity_sdk/'+('metadata.inc' if metadata else 'hooks.inc')))
                fixture = fixture.replace('    osi[21]=(void *)lock;',
                    '    g_ic['+str((0x1d0 if target=='esp32c5' else 0x1c0)//4)+']=(u32)completed;\n    osi[21]=(void *)lock;')
                fixture = fixture.replace('    return 0;\n}\n__attribute__',
                    '    CHECK(allocated_hooks==posted && completed_hooks==posted && public_callbacks==posted,90);\n    return 0;\n}\n__attribute__')
                if metadata:
                    from tests.c.integration.wifi.monitor.test_wifi_rx_target import ROOT, INTERNAL, COMMON, unit
                    raw = ROOT/'components/esp32_mquickjs/src/modules/wifi_raw_tx'
                    symbols = json.loads((ROOT/'docs/idf-wifi-api-inventory.json').read_text())['variants']['esp32c5/representative']['symbols']
                    types = {key.split('::')[-1]: value['declaration'] for key, value in symbols.items()}
                    prefix = '#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <string.h>\ntypedef uint16_t u16;\n'
                    for name in ('wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t', 'wifi_tx_info_t', 'esp_80211_tx_info_t'):
                        prefix += types[name]+';\n'
                    for name in ('wifi_rx', 'wifi_raw_tx_validate', 'wifi_raw_tx_snapshot'):
                        prefix += unit(INTERNAL/('esp32_mquickjs_'+name+'.h'))
                    prefix += unit(COMMON/'esp32_mquickjs_wifi_rx.c')
                    prefix += unit(raw/'esp32_mquickjs_wifi_raw_tx_validate.c')
                    prefix += unit(raw/'esp32_mquickjs_wifi_raw_tx_snapshot.c')
                    broker = (raw/'esp32_mquickjs_wifi_raw_tx_broker.c').read_text()
                    fixture = fixture.replace('/* Production descriptor observation hook is inserted here. */',
                        extract(broker, 'raw_tx_read_mac_status_byte') + extract(broker, 'esp32qjs_raw_tx_info'))
                    fixture = fixture.replace('static const u8 mac[6]={2,3,4,5,6,7};',
                        'static const u8 mac[6]={0x10,0xbd,0xa3,0xc8,0x54,0xe8};')
                    fixture = fixture[:fixture.index('int main(void)')] + \
                        fixture_text('wifi/tx/test_wifi_raw_tx_identity_sdk/reported_frames.inc') + \
                        fixture_text('wifi/tx/test_wifi_raw_tx_identity_sdk/metadata_main.inc') + \
                        fixture[fixture.index('__attribute__'):]
                    fixture = prefix+fixture
                link_flags = ['-Wl,--gc-sections,-e,_start']
                if cache:
                    fixture = '__attribute__((naked,noreturn)) void fixture_exit(int code) {__asm__ volatile("li a7,93\\necall\\n");}\n'+fixture
                    start = fixture.index('void *ic_ebuf_alloc(')
                    end = fixture.index('void *chm_get_current_channel', start)
                    fixture = fixture[:start]+fixture_text('wifi/tx/test_wifi_raw_tx_identity_sdk/cache_alloc.inc')+fixture[end:]
                    fixture = fixture.replace('void fixture_post_hmac_tx(void *buffer) {',
                        'void fixture_post_hmac_tx(void *buffer) {\n    u32 *eb=buffer; u32 *tx_info=(u32 *)eb[EB_TXINFO_WORD];')
                    fixture = fixture.replace('if(buffer!=bound || allocated_hooks!=posted+1) __builtin_trap();',
                        'if(buffer!=bound) fixture_exit(71);\n    if(allocated_hooks!=posted+1) __builtin_trap();')
                    start = fixture.index('int main(void)')
                    fixture = fixture[:start]+fixture_text('wifi/tx/test_wifi_raw_tx_identity_sdk/cache_boundaries.inc')+fixture[start:]
                    fixture = fixture.replace('    channel=4;', '    CHECK(ieee80211_output_init()==0 && registered_output,14);\n    channel=4;')
                    fixture = fixture.replace('                    CHECK(!metadata_failure,20+metadata_failure);',
                        '                    CHECK(registered_output()==0,13);\n                    CHECK(!metadata_failure,20+metadata_failure);')
                    fixture = fixture.replace('    CHECK(locks>0 && locks==unlocks,31);',
                        '    CHECK(pool_allocations==expected_completions*2 && cache_recycles==expected_completions && identity_transfers==expected_completions,32);\n    CHECK(locks>0 && locks==unlocks,31);')
                    for name,replacement in (
                        ('ieee80211_search_node','fixture_search_node'),
                        ('ieee80211_output_raw_process','fixture_raw_process'),
                        ('ieee80211_output_pending_eb','fixture_unexpected_path'),
                        ('ieee80211_wapi_alloc_tx_buf','fixture_unexpected_path'),
                        ('is_wapi_alloc_tx_buf','fixture_unexpected_path'),
                        ('ieee80211_encap_esfbuf','fixture_unexpected_path'),
                        ('ieee80211_classify','fixture_unexpected_path'),
                        ('ieee80211_encap_amsdu','fixture_amsdu_noop'),
                        ('ieee80211_pwrsave','fixture_unexpected_path'),
                        ('nan_dp_post_tx','fixture_unexpected_path'),
                        ('ppTxPkt','fixture_unexpected_path'),
                    ):
                        link_flags.append('-Wl,--defsym='+name+'='+replacement)
                    # Model the target ROM assignment. The real SDK init must
                    # register the hidden instrumented origin, not this trap.
                    link_flags.append('-Wl,--defsym=ieee80211_output_process=fixture_unexpected_path')
                else:
                    link_flags.append('-Wl,--defsym=ieee80211_post_hmac_tx=fixture_post_hmac_tx')
                (root/'fixture.c').write_text(fixture)
                built = subprocess.run([cc, '-Os', '-march=rv32imac', '-mabi=ilp32', '-mno-relax', '-nostdlib',
                    '-ffunction-sections', '-fdata-sections', '-fno-builtin', '-DEXPECT_EXTENDED=1',
                    '-DCONFIG_ESP32_MQUICKJS_FEATURE_WIFI=1', '-DESP32_MQUICKJS_RAW_TX_EXTENDED_MANAGEMENT=1',
                    '-DCONFIG_IDF_TARGET_ESP32C5='+str(int(target=='esp32c5')),
                    '-DEB_TXINFO_WORD='+('14' if target=='esp32c5' else '11'), str(root/'fixture.c'),
                    str(root/'ieee80211_output.o'), str(root/'ieee80211.o'),
                    *link_flags,
                    '-o', str(root/'test.elf')], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([qemu, str(root/'test.elf')], capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, 'SDK fixture exit '+str(result.returncode)+' '+result.stderr)
