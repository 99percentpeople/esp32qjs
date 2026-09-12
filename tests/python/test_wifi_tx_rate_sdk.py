"""Execute the pinned SDK rate validator, including its stopped-driver path."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_wifi_csi_rx_link import tool, elf
from test_wifi_tx_rate import rate_code

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))

FIXTURE = r'''
#include "esp_wifi.h"
unsigned char g_ic[24];
unsigned char nvs[1500], chm[700], node[400];
unsigned char *g_wifi_nvs=nvs, *g_chm=chm;
static int band=0, initialized=1, reads, writes, getter_error;
static unsigned protocols=7;
static int protocols_5g=-1, protocols_ap=-1, written_interface=-1;
static wifi_tx_rate_config_t written;
int fixture_initialized(void) { return initialized; }
int chm_get_current_band(void) { return band; }
int fixture_get_protocol(wifi_interface_t interface, unsigned char *output)
{ reads++; *output=interface==WIFI_IF_AP && protocols_ap>=0?(unsigned)protocols_ap:protocols; return getter_error; }
#if CONFIG_SOC_WIFI_SUPPORT_5G
int fixture_get_protocols(wifi_interface_t interface, wifi_protocols_t *output)
{ reads++; output->ghz_2g=interface==WIFI_IF_AP && protocols_ap>=0?(unsigned)protocols_ap:protocols; output->ghz_5g=protocols_5g<0?protocols:(unsigned)protocols_5g; return getter_error; }
#endif
void wifi_log(int a, int b, int c, const char *s, ...) { (void)a; (void)b; (void)c; (void)s; }
void ic_set_80211_tx_rate_config(unsigned char interface, const wifi_tx_rate_config_t *value)
{ written_interface=interface; writes++; written=*value; }
void *memcpy(void *out,const void *in,unsigned n)
{ unsigned char *d=out; const unsigned char *s=in; while(n--) *d++=*s++; return out; }
int esp_wifi_config_80211_tx(wifi_interface_t, wifi_tx_rate_config_t *);
#define CHECK(expr, code) do { if (!(expr)) return code; } while(0)
int main(void)
{
    wifi_tx_rate_config_t config={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M};
    /* STOP has destroyed both interface objects. The SDK API promises that
     * init-before-start configuration works, without manufacturing an object. */
    for (int interface=0; interface<2; interface++) {
        CHECK(esp_wifi_config_80211_tx(interface,&config)==ESP_OK,10+interface);
        CHECK(writes==interface+1 && written.rate==WIFI_PHY_RATE_6M,12);
    }
    CHECK(reads==2,13);
    band=1;
    initialized=0;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_ERR_WIFI_NOT_INIT && writes==2,14);
    initialized=1;
    CHECK(esp_wifi_config_80211_tx(2,&config)!=ESP_OK && reads==2,15);
    CHECK(esp_wifi_config_80211_tx(0,0)!=ESP_OK && reads==2,16);
    /* Getter failure is a real pre-write error, not a guessed PHY/default. */
    getter_error=ESP_ERR_NO_MEM;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_ERR_NO_MEM && writes==2,17);
    getter_error=0;
    protocols=1; config.phymode=WIFI_PHY_MODE_HT20; config.rate=WIFI_PHY_RATE_MCS0_LGI;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && writes==2,18);
    protocols=7;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_OK && writes==3,19);
    config.phymode=WIFI_PHY_MODE_HT40;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && writes==3,20);
    nvs[157]=2; chm[SECONDARY_OFFSET]=1;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_OK && writes==4,21);
    /* The interface has not enabled LR. */
    config.phymode=WIFI_PHY_MODE_LR; config.rate=WIFI_PHY_RATE_LORA_250K; protocols=7;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && writes==4,22);
    /* A live interface keeps the original SDK PHY source; no getter or saved
     * protocol replaces its runtime value. */
    *(void **)(g_ic+16)=node; node[PHY_OFFSET]=1; protocols=7;
    config.phymode=WIFI_PHY_MODE_HT20; config.rate=WIFI_PHY_RATE_MCS0_LGI;
    int before=reads;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && reads==before && writes==4,24);
    *(void **)(g_ic+16)=0;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    protocols=71; config.phymode=WIFI_PHY_MODE_HE20; config.ersu=true; config.dcm=false;
    config.rate=WIFI_PHY_RATE_MCS3_LGI;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && writes==4,25);
    config.rate=WIFI_PHY_RATE_MCS0_LGI;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_OK && writes==5,26);
    config.ersu=false; config.dcm=true; config.rate=WIFI_PHY_RATE_MCS2_LGI;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && writes==5,27);
    config.dcm=false; band=2; protocols=16;
    config.phymode=WIFI_PHY_MODE_11A; config.rate=WIFI_PHY_RATE_6M;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_OK && writes==6,28);
    config.phymode=WIFI_PHY_MODE_11B; config.rate=WIFI_PHY_RATE_1M_L;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && writes==6,29);
    protocols=116; config.phymode=WIFI_PHY_MODE_HE20; config.rate=WIFI_PHY_RATE_MCS0_LGI;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_OK && writes==7,30);
    band=0; protocols=71; protocols_5g=116;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_OK && writes==8,31);
    protocols_5g=20;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_ERR_INVALID_STATE && writes==8,32);
    config.phymode=WIFI_PHY_MODE_11A; config.rate=WIFI_PHY_RATE_6M;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_OK && writes==9,33);
    protocols_5g=0;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_ERR_INVALID_STATE && writes==9,34);
#endif
    /* Actual SDK LR validation: only the selected interface's protocol flag
     * enables LR. Neither the other interface nor an unrelated NVS byte can
     * authorize it. Exercise fresh AUTO, STOP and existing interface objects. */
    for (int live=0; live<2; live++) {
        *(void **)(g_ic+16)=live?node:0;
        *(void **)(g_ic+20)=live?node:0;
        node[PHY_OFFSET]=3;
        for (band=0; band<=1; band++) {
            for (int interface=0; interface<2; interface++) {
                for (int mixed=0; mixed<2; mixed++) {
                    protocols=interface==0?(mixed?15:8):7;
                    protocols_ap=interface==1?(mixed?15:8):7;
                    nvs[1]=0; config.ersu=false; config.dcm=false;
                    config.phymode=WIFI_PHY_MODE_LR;
                    for (int rate=WIFI_PHY_RATE_LORA_250K; rate<=WIFI_PHY_RATE_LORA_500K; rate++) {
                        config.rate=rate; int count=writes;
                        CHECK(esp_wifi_config_80211_tx(interface,&config)==ESP_OK,40);
                        CHECK(writes==count+1 && written_interface==interface &&
                            written.phymode==WIFI_PHY_MODE_LR && written.rate==rate &&
                            !written.ersu && !written.dcm,41);
                        nvs[1]=1; count=writes;
                        CHECK(esp_wifi_config_80211_tx(1-interface,&config)!=ESP_OK && writes==count,42);
                        nvs[1]=0;
                    }
                    int count=writes;
                    for (int rate=-1; rate<=43; rate++) {
                        if (rate==WIFI_PHY_RATE_LORA_250K || rate==WIFI_PHY_RATE_LORA_500K) continue;
                        config.rate=rate;
                        CHECK(esp_wifi_config_80211_tx(interface,&config)!=ESP_OK && writes==count,43);
                    }
                    config.rate=WIFI_PHY_RATE_LORA_250K;
                    config.ersu=true;
                    CHECK(esp_wifi_config_80211_tx(interface,&config)!=ESP_OK && writes==count,44);
                    config.ersu=false; config.dcm=true;
                    CHECK(esp_wifi_config_80211_tx(interface,&config)!=ESP_OK && writes==count,45);
                    config.dcm=false; getter_error=ESP_ERR_NO_MEM;
                    CHECK(esp_wifi_config_80211_tx(interface,&config)==ESP_ERR_NO_MEM && writes==count,46);
                    getter_error=0;
                }
            }
        }
    }
#if CONFIG_SOC_WIFI_SUPPORT_5G
    band=2; protocols=15; protocols_ap=15; protocols_5g=116;
    int count=writes;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && writes==count,47);
    CHECK(esp_wifi_config_80211_tx(1,&config)!=ESP_OK && writes==count,48);
#endif
    return 0;
}
__attribute__((naked,noreturn)) void _start(void)
{ __asm__ volatile("call main\nli a7,93\necall\n"); }
'''


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
                (root/'esp_wifi.h').write_text('#pragma once\n'+header+'''
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM 0x101
#undef ESP_ERR_INVALID_ARG
#undef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_WIFI_NOT_INIT 0x3001
#define WIFI_PROTOCOL_LR 8
#define WIFI_PROTOCOL_11B 1
#define WIFI_PROTOCOL_11G 2
#define WIFI_PROTOCOL_11N 4
#define WIFI_PROTOCOL_11A 16
#define WIFI_PROTOCOL_11AC 32
#define WIFI_PROTOCOL_11AX 64
typedef struct { uint16_t ghz_2g,ghz_5g; } wifi_protocols_t;
int esp_wifi_get_protocol(wifi_interface_t,unsigned char *);
int esp_wifi_get_protocols(wifi_interface_t,wifi_protocols_t *);
''')
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
