"""Production Radio composite transaction with SDK faults; Wi-Fi phase pending.

Uses recorded SDK declarations and the actual transaction, exact-token admission,
validators, semantic comparison and secure-zero implementation. SDK getter/setter
storage is injected; it does not establish actual SDK normalization/NVS behavior.
"""
from tests.support.fixtures import fixture_text
import json
import re
import unittest
from tests.support.wireless_vm_fixture import ROOT, extract
from tests.support.native_compile import compile_run

RADIO = ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c'
HEADER = ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_radio.h'


def structure(source, name):
    return re.search(r'typedef struct \{[^}]*\} ' + name + r';', source).group(0) + '\n'


def sdk_types(variant, extra=()):
    symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][variant]['symbols']
    declarations = {k.split('::')[-1]: v['declaration'] for k, v in symbols.items()
                    if v.get('declaration', '').startswith('typedef ')}
    seen, output = set(), []

    def visit(name):
        if name in seen:
            return
        seen.add(name)
        declaration = declarations[name]
        for dependency in sorted(set(re.findall(r'\b\w+_t\b', declaration)) - {name}):
            if dependency in declarations:
                visit(dependency)
        output.append(declaration + ';')

    for name in ('wifi_country_t', 'wifi_protocols_t', 'wifi_bandwidths_t', 'wifi_band_mode_t',
                 'wifi_ps_type_t', 'wifi_mode_t', 'wifi_interface_t', 'wifi_storage_t', 'wifi_config_t') + tuple(extra):
        visit(name)
    return '\n'.join(output) + '\n'


PRELUDE = fixture_text('wifi/config/test_wifi_config_controls/prelude.inc')

BOUNDARIES = fixture_text('wifi/config/test_wifi_config_controls/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_config_controls/main.inc')


class WiFiConfigControls(unittest.TestCase):
    def test_production_transaction_boundaries(self):
        source, header = RADIO.read_text(), HEADER.read_text()
        local = '\n'.join(structure(header, name) for name in (
            'esp32_mquickjs_wifi_radio_lifecycle_t', 'esp32_mquickjs_wifi_radio_config_result_t',
            'esp32_mquickjs_wifi_radio_config_controls_t'))
        local += '\ntypedef bool (*esp32_mquickjs_wifi_config_accept_fn)(const wifi_config_t *,const wifi_config_t *);\n'
        helpers = source[source.index('/* These helpers run inside'):source.index('/* Caller has already validated')]
        # Public driver wrappers have their own admission fixtures; keep this
        # transaction fixture focused on the production composite helpers.
        for name in ('esp32_mquickjs_wifi_radio_read_phy', 'esp32_mquickjs_wifi_radio_write_phy'):
            helpers = helpers.replace(extract(source, name), '')
        production = extract(source, 'esp32_mquickjs_wifi_radio_5ghz_channel_bit')
        production += extract(source, 'wifi_radio_validate_regulatory_channel')
        production += extract(source, 'esp32_mquickjs_wifi_radio_pmf_disable_allowed')
        production += extract(source, 'wifi_radio_restore_disabled_pmf')
        production += extract(source, 'wifi_radio_config_equal') + helpers
        production += extract(source, 'wifi_radio_configure_locked')
        production += extract(source, 'esp32_mquickjs_wifi_radio_configure_lifecycle')
        zero = extract((ROOT / 'components/esp32_mquickjs/src/core/esp32_mquickjs_wireless_core.c').read_text(),
                       'esp32_mquickjs_wireless_secure_zero')
        for target, he, five in [('esp32c3', 0, 0), ('esp32c5', 1, 1)]:
            with self.subTest(target=target):
                gates = (f'\n#define CONFIG_SOC_WIFI_SUPPORT_5G {five}\n'
                         f'#define CONFIG_SOC_WIFI_HE_SUPPORT {he}\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n')
                compile_run(self, PRELUDE + gates + sdk_types(target + '/representative') + local +
                            BOUNDARIES + zero + production + MAIN)
