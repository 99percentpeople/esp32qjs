"""Deferred production FTM physical recovery and original Session retirement.

Real Radio admission/STOP/shutdown/timer barriers/retirement plus real Session
worker. SDK, event/timer/worker scheduling, locks and unrelated broker storage are
injected boundaries. No replacement recovery FSM or SDK/RF proof. AST only.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.ftm.test_wifi_ftm_radio import ftm_code, MAIN as FTM_MAIN
from tests.c.integration.wifi.tx.test_wifi_action_recovery import BOUNDARIES as RECOVERY_BOUNDARIES
from tests.c.integration.wifi.ftm.test_wifi_ftm_session import BOUNDARIES as SESSION_BOUNDARIES, MAIN as SESSION_MAIN
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract
from tests.c.integration.wifi.lifecycle.test_wifi_recovery_runtime import recovery_request_code


def recovery_code(profile, ap):
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = '#define CONFIG_IDF_TARGET_' + profile.split('/')[0].upper() + ' 1\n' + ftm_code(profile, ap)
    code = code.replace('bool uncertain;} s_interval;', 'bool uncertain,restore_pending;} s_interval;')
    code = code.replace('struct {unsigned identity,generation;} s_tx_rate_lease;',
                        'struct {unsigned identity,generation;bool restore_pending;} s_tx_rate_lease;')
    code = code.replace('bool event_fence_posted,event_fence_seen;', fixture_text('wifi/ftm/test_wifi_ftm_recovery/recovery_code-code.inc'))
    code = code.replace('assert(locks && !critical);critical=1;', 'assert(!critical);critical=1;')
    code = code.replace('assert(locks && critical);critical=0;', 'assert(critical);critical=0;')
    boundaries = RECOVERY_BOUNDARIES.replace('s_action.lease.acquired', 's_ftm.lease.acquired')
    code += boundaries
    code += 'static bool wifi_radio_action_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *);\n'
    code += extract(source, 'wifi_radio_ftm_recovery_timer_locked')
    for name in ('wifi_radio_record_fault', 'wifi_radio_cleanup_fault', 'wifi_radio_action_recovery_exact_locked',
                 'wifi_radio_ftm_recovery_capture_owner_locked', 'esp32_mquickjs_wifi_radio_ftm_recovery_active',
                 'esp32_mquickjs_wifi_radio_begin_ftm_recovery', 'wifi_radio_stop_owners_locked', 'wifi_radio_stop_lease_locked', 'wifi_radio_stop_locked',
                 'wifi_radio_shutdown_lease_locked', 'wifi_radio_shutdown_locked', 'wifi_radio_ftm_recovery_phase',
                 'esp32_mquickjs_wifi_radio_stop_ftm_recovery', 'esp32_mquickjs_wifi_radio_shutdown_ftm_recovery',
                 'esp32_mquickjs_wifi_radio_check_stopped_ftm_recovery', 'esp32_mquickjs_wifi_radio_finish_ftm_recovery'):
        code += extract(source, name)
    # Route through production kind selection and both native exact-token guards.
    # Checkpoint snapshot/replay storage remains covered by its separate fixtures.
    code = code.replace('typedef struct {uint32_t generation;} esp32_mquickjs_wifi_raw_tx_broker_status_t;',
        'typedef struct {uint32_t generation;struct {uint32_t generation,identity,radio_lease_identity;} token;} esp32_mquickjs_wifi_raw_tx_broker_status_t;')
    # No Raw TX owner in this fixture: unrelated subsystem admission rejects.
    code += fixture_text('wifi/ftm/test_wifi_ftm_recovery/recovery_code.inc')
    for phase in ('stop','check_stopped','shutdown','finish'):
        code += 'static int esp32_mquickjs_wifi_radio_' + phase + '_raw_tx_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return ESP_ERR_INVALID_STATE;}\n'
    code += recovery_request_code()
    for name in ('esp32_mquickjs_wifi_radio_action_recovery_active',
                 'esp32_mquickjs_wifi_radio_check_stopped_action_recovery',
                 'esp32_mquickjs_wifi_radio_begin_action_recovery',
                 'wifi_radio_action_recovery_phase', 'esp32_mquickjs_wifi_radio_stop_action_recovery',
                 'esp32_mquickjs_wifi_radio_shutdown_action_recovery',
                 'esp32_mquickjs_wifi_radio_finish_action_recovery',
                 'esp32_mquickjs_wifi_radio_begin_recovery', 'esp32_mquickjs_wifi_radio_recovery_active',
                 'esp32_mquickjs_wifi_radio_stop_recovery', 'esp32_mquickjs_wifi_radio_check_stopped_recovery',
                 'esp32_mquickjs_wifi_radio_shutdown_recovery', 'esp32_mquickjs_wifi_radio_finish_recovery'):
        code += extract(source, name)
    code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_ftm_session.h')
    code += SESSION_BOUNDARIES
    code += unit(COMPONENT / 'src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm_session.c')
    code += FTM_MAIN[:FTM_MAIN.index('int main(void)')]
    # Real Session worker/scheduler helpers, without the unrelated close-hook helper.
    helpers = SESSION_MAIN[SESSION_MAIN.index('static void work(void)'):SESSION_MAIN.index('int main(void)')]
    return code + helpers


class WiFiFtmRecovery(unittest.TestCase):
    def test_missing_or_ambiguous_report_physical_retirement_and_cleanup_suffix(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    compile_run(self, recovery_code(profile, ap) + MAIN)


MAIN = fixture_text('wifi/ftm/test_wifi_ftm_recovery/main.inc')
