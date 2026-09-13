"""Deferred real FTM offset ledger/Radio admission and restart suffix fixtures.

Only SDK, lock and storage boundaries are injected. No alternative lifecycle
implementation, SDK IPC execution or RF proof. AST parse only until Wi-Fi stage.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_policy import policy_code
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.config.test_wifi_policy_record import PRELUDE
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, build, extract, run

GATES = '\n#define CONFIG_ESP_WIFI_FTM_ENABLE 1\n#define CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT 1\n'


def ledger_code():
    return unit(COMPONENT / 'internal/esp32_mquickjs_wifi_ftm_offset.h') + unit(
        COMPONENT / 'src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm_offset.c')


def offset_code(profile):
    code = GATES + policy_code(profile, True, False)
    code = code.replace('struct {unsigned identity;} operation,lifecycle;',
                        'struct {unsigned identity,generation;} operation,lifecycle;')
    code += ledger_code()
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code += 'static esp32_mquickjs_wifi_ftm_offset_state_t s_ftm_offset;\n'
    code += re.search(r'static struct \{[^}]*\} s_policy_restart;', source).group(0)
    code += RADIO_BOUNDARIES
    for name in ('wifi_radio_ftm_offset_writer', 'esp32_mquickjs_wifi_radio_ftm_offset_status',
                 'esp32_mquickjs_wifi_radio_write_ftm_offset', 'wifi_radio_policy_restart_prepare_locked',
                 'wifi_radio_policy_restart_replay_locked'):
        code += extract(source, name)
    return code


class WiFiFtmOffset(unittest.TestCase):
    def test_production_mutation_boundary_preserves_only_reviewed_target_stop_payloads(self):
        source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        boundary = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio_mutation.h').read_text()
        for target in ('ESP32C3', 'ESP32S3', 'ESP32C5', 'UNREVIEWED'):
            code = '#include <stdbool.h>\n#include <stdint.h>\n#include <assert.h>\n'
            code += '#define CONFIG_IDF_TARGET_' + target + ' 1\n'
            code += 'static struct {bool unchanged;} s_stop_snapshot;\n'
            code += 'static int native;static int esp_wifi_ftm_resp_set_offset(int16_t n){native=n;return 77;}\n'
            code += extract(source, 'wifi_radio_invalidate_stop_snapshot_locked') + boundary
            expected = 'false' if target == 'UNREVIEWED' else 'true'
            code += 'int main(void){s_stop_snapshot.unchanged=true;assert(esp_wifi_ftm_resp_set_offset(-7)==77);'
            code += 'assert(native==-7 && s_stop_snapshot.unchanged==' + expected + ');return 0;}\n'
            compile_run(self, code)

    def test_real_ledger_signed_values_failure_history_replay_and_exhaustion(self):
        compile_run(self, PRELUDE + GATES + '\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n' + ledger_code() + LEDGER_MAIN)

    def test_real_radio_admission_snapshot_exact_lifecycle_and_prestart_replay(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                code = offset_code(profile)
                compile_run(self, code + RADIO_MAIN)

    def test_public_signed_capture_status_errors_and_nth_gc_allocation(self):
        source = (COMPONENT / 'src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm_module.c').read_text()
        code = re.sub(r'\bcalls\b', 'sdk_calls', offset_code('esp32c5/representative'))
        code = re.sub(r'\bfail_at\b', 'sdk_fail_at', code)
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += 'static const char *esp_err_to_name(int error){(void)error;return "injected";}\n'
        code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        for name in ('ftm_offset_to_js', 'js_wifi_ftm_responder_offset_status', 'js_wifi_ftm_set_responder_offset'):
            code += extract(source, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for scenario in ('-32768', '32767', '0', '-32769', '32768', '1.5', 'NaN', 'Infinity', '"1"', 'null', 'arity', 'sdk-error', 'status'):
                with self.subTest(scenario=scenario):run([str(binary), scenario])


LEDGER_MAIN = fixture_text('wifi/ftm/test_wifi_ftm_offset/ledger_main.inc')

RADIO_BOUNDARIES = fixture_text('wifi/ftm/test_wifi_ftm_offset/radio_boundaries.inc')

RADIO_MAIN = fixture_text('wifi/ftm/test_wifi_ftm_offset/radio_main.inc')

VM_MAIN = fixture_text('wifi/ftm/test_wifi_ftm_offset/vm_main.inc')
