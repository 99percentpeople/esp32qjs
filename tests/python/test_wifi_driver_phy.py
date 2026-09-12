"""Deferred production Radio PHY readback tests; implementation batch: AST only."""
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from wireless_vm_fixture import extract

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / 'components/esp32_mquickjs'


def phy_types(profile, ap=True):
    symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][profile]['symbols']
    declarations = {key.split('::')[-1]: item['declaration'] for key, item in symbols.items()}
    code = '#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>\n#include <assert.h>\n'
    code += f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {int(ap)}\n'
    code += f'#define CONFIG_SOC_WIFI_SUPPORT_5G {int(profile.startswith("esp32c5/"))}\n'
    for name in ('wifi_interface_t', 'wifi_mode_t', 'wifi_band_mode_t', 'wifi_bandwidth_t', 'wifi_bandwidths_t', 'wifi_protocols_t'):
        code += declarations[name] + ';\n'
    for name in ('11B', '11G', '11N', '11A', '11AC', '11AX', 'LR'):
        code += '#define ' + declarations['WIFI_PROTOCOL_' + name] + '\n'
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    for kind, name in (('enum', 'esp32_mquickjs_wifi_phy_query_t'),
                       ('struct', 'esp32_mquickjs_wifi_phy_readback_t'),
                       ('enum', 'esp32_mquickjs_wifi_radio_driver_state_t')):
        code += re.search(r'typedef ' + kind + r' \{[^}]*\} ' + name + ';', header).group(0) + '\n'
    return code


class WiFiDriverPhy(unittest.TestCase):
    def test_sdk_read_errors_admission_inactive_bands_and_no_partial_output(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap), tempfile.TemporaryDirectory() as tmp:
                    path, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                    path.write_text(phy_types(profile, ap) + BOUNDARIES +
                                    extract(source, 'esp32_mquickjs_wifi_radio_read_phy') + MAIN)
                    result = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                             str(path), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stderr)


BOUNDARIES = r'''
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_WIFI_NOT_INIT 4
#define ESP_ERR_INVALID_RESPONSE 5
static unsigned locks, reads;
static int sdk_error;
static uint16_t bits=WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G;
static wifi_bandwidth_t width=WIFI_BW40;
static wifi_band_mode_t band_mode=WIFI_BAND_MODE_2G_ONLY;
static struct {
    bool driver_owned,storage_configured,restart_required;
    struct {unsigned identity;} lifecycle,operation;
    const char *fault_stage,*cleanup_stage;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    wifi_mode_t effective_mode;
} s_radio={.driver_owned=true,.storage_configured=true,
    .driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED,.effective_mode=WIFI_MODE_APSTA};
static void wifi_radio_operation_lock(void) {assert(!locks);++locks;}
static void wifi_radio_operation_unlock(void) {assert(locks==1);--locks;}
static int boundary(wifi_interface_t iface) {assert(locks==1 && (iface==WIFI_IF_STA || iface==WIFI_IF_AP));++reads;return sdk_error;}
static int esp_wifi_get_protocol(wifi_interface_t iface,uint8_t *out) {*out=bits;int err=boundary(iface);return err ? err : band_mode==WIFI_BAND_MODE_AUTO ? ESP_ERR_NOT_SUPPORTED : ESP_OK;}
static int esp_wifi_get_bandwidth(wifi_interface_t iface,wifi_bandwidth_t *out) {*out=width;int err=boundary(iface);return err ? err : band_mode==WIFI_BAND_MODE_AUTO ? ESP_ERR_NOT_SUPPORTED : ESP_OK;}
#if CONFIG_SOC_WIFI_SUPPORT_5G
static int esp_wifi_get_band_mode(wifi_band_mode_t *out) {assert(locks==1);++reads;*out=band_mode;return sdk_error;}
static int esp_wifi_get_protocols(wifi_interface_t iface,wifi_protocols_t *out) {
    *out=(wifi_protocols_t){.ghz_2g=0xffff,.ghz_5g=0xffff};
    if(band_mode!=WIFI_BAND_MODE_5G_ONLY)out->ghz_2g=bits;
    if(band_mode!=WIFI_BAND_MODE_2G_ONLY)out->ghz_5g=WIFI_PROTOCOL_11A|WIFI_PROTOCOL_11N;
    return boundary(iface);
}
static int esp_wifi_get_bandwidths(wifi_interface_t iface,wifi_bandwidths_t *out) {
    *out=(wifi_bandwidths_t){.ghz_2g=99,.ghz_5g=99};
    if(band_mode!=WIFI_BAND_MODE_5G_ONLY)out->ghz_2g=width;
    if(band_mode!=WIFI_BAND_MODE_2G_ONLY)out->ghz_5g=WIFI_BW20;
    return boundary(iface);
}
#endif
'''

MAIN = r'''
int main(void) {
    esp32_mquickjs_wifi_phy_readback_t out,zero={0};const char *stage;
#define QUERY(kind) esp32_mquickjs_wifi_radio_read_phy(WIFI_IF_STA,ESP32_MQUICKJS_WIFI_PHY_##kind,&out,&stage)
    assert(QUERY(PROTOCOL)==ESP_OK && out.protocols.ghz_2g==bits && !stage && !locks);
    assert(QUERY(BANDWIDTH)==ESP_OK && out.bandwidths.ghz_2g==WIFI_BW40);
    assert(QUERY(PROTOCOLS)==ESP_OK && out.bands==1 && out.protocols.ghz_5g==0);
    assert(QUERY(BANDWIDTHS)==ESP_OK && out.bands==1 && out.bandwidths.ghz_5g==0);
    band_mode=WIFI_BAND_MODE_AUTO;
    assert(QUERY(PROTOCOL)==ESP_ERR_NOT_SUPPORTED && !memcmp(&out,&zero,sizeof(out)));
    assert(QUERY(BANDWIDTH)==ESP_ERR_NOT_SUPPORTED);
#if CONFIG_SOC_WIFI_SUPPORT_5G
    assert(QUERY(PROTOCOLS)==ESP_OK && out.bands==3 && out.protocols.ghz_5g==(WIFI_PROTOCOL_11A|WIFI_PROTOCOL_11N));
    assert(QUERY(BANDWIDTHS)==ESP_OK && out.bands==3 && out.bandwidths.ghz_2g==WIFI_BW40 && out.bandwidths.ghz_5g==WIFI_BW20);
    band_mode=WIFI_BAND_MODE_5G_ONLY;
    assert(QUERY(PROTOCOLS)==ESP_OK && out.bands==2 && !out.protocols.ghz_2g);
    assert(QUERY(BANDWIDTHS)==ESP_OK && out.bands==2 && !out.bandwidths.ghz_2g);
    band_mode=99;
    assert(QUERY(PROTOCOLS)==ESP_ERR_INVALID_RESPONSE && !strcmp(stage,"band-mode"));
#endif
    band_mode=WIFI_BAND_MODE_2G_ONLY;
    sdk_error=89;assert(QUERY(PROTOCOL)==89 && !strcmp(stage,"protocol") && !memcmp(&out,&zero,sizeof(out)));
    assert(QUERY(BANDWIDTHS)==89 && !memcmp(&out,&zero,sizeof(out)));sdk_error=0;
    bits=0x80;assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_RESPONSE && !strcmp(stage,"decode"));bits=WIFI_PROTOCOL_11B;
    width=99;assert(QUERY(BANDWIDTH)==ESP_ERR_INVALID_RESPONSE);width=WIFI_BW20;
    unsigned before=reads;
    s_radio.driver_owned=false;assert(QUERY(PROTOCOL)==ESP_ERR_WIFI_NOT_INIT);s_radio.driver_owned=true;
    s_radio.lifecycle.identity=1;assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_STATE);s_radio.lifecycle.identity=0;
    s_radio.operation.identity=1;assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_STATE);s_radio.operation.identity=0;
    s_radio.fault_stage="fault";assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_STATE);s_radio.fault_stage=NULL;
    s_radio.cleanup_stage="cleanup";assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_STATE);s_radio.cleanup_stage=NULL;
    s_radio.restart_required=true;assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_STATE);s_radio.restart_required=false;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTING;assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_STATE);
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;s_radio.effective_mode=WIFI_MODE_AP;
    assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_STATE);s_radio.effective_mode=WIFI_MODE_APSTA;
    assert(esp32_mquickjs_wifi_radio_read_phy(99,ESP32_MQUICKJS_WIFI_PHY_PROTOCOL,&out,&stage)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_read_phy(WIFI_IF_STA,99,&out,&stage)==ESP_ERR_INVALID_ARG);
    assert(reads==before && !locks);
    int err=esp32_mquickjs_wifi_radio_read_phy(WIFI_IF_AP,ESP32_MQUICKJS_WIFI_PHY_PROTOCOL,&out,&stage);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(err==ESP_OK && reads==before+1);
#else
    assert(err==ESP_ERR_NOT_SUPPORTED && reads==before);
#endif
    return 0;
}
'''
