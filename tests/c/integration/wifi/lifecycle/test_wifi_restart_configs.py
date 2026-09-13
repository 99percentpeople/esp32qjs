"""Deferred production credential/global checkpoint; injected SDK/heap, no device proof."""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.station.test_wifi_connection_controls import control_code
from tests.c.integration.wifi.station.test_wifi_scan_parameters import scan_support
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, extract


def config_code(profile, ap, mutation_boundary=False, he_statistics=False):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    code = f'#define CONFIG_SOC_WIFI_HE_SUPPORT {int(profile.startswith("esp32c5/"))}\n'
    code += f'#define CONFIG_IDF_TARGET_{profile.split("/")[0].upper()} 1\n'
    code += '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_SOC_WIFI_SUPPORTED 1\n#include <stddef.h>\n'
    code += f'#define CONFIG_SOC_WIFI_SUPPORT_5G {int(profile.startswith("esp32c5/"))}\n'
    code += control_code(profile, ap, ('wifi_phy_rate_t', 'wifi_phy_mode_t', 'wifi_tx_rate_config_t', 'wifi_band_t', 'wifi_second_chan_t', 'wifi_scan_default_params_t'))
    rate_unit = unit(COMPONENT / 'internal/esp32_mquickjs_wifi_tx_rate.h')
    rate_unit += unit(COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_tx_rate.c')
    code = code.replace('static struct {unsigned identity;} s_tx_rate_lease;',
                        rate_unit + '\nstatic esp32_mquickjs_wifi_tx_rate_lease_t s_tx_rate_lease;')
    code += f'\n#define CONFIG_SOC_WIFI_SUPPORT_5G {int(profile.startswith("esp32c5/"))}\n'
    code = code.replace('struct {unsigned identity;} operation,lifecycle;',
                        'struct {unsigned identity,generation;} operation,lifecycle;')
    code = code.replace('wifi_mode_t effective_mode;',
                        'wifi_mode_t effective_mode;bool stop_required,stop_submitted;uint32_t event_identity;unsigned event_phase,event_live;')
    # Storage belongs to the shared connection-control state. These SDK
    # boundaries enforce START/interface admission for the production helpers.
    code = code.replace(extract(code, 'esp_wifi_get_inactive_time'), INACTIVE_GET)
    code = code.replace(extract(code, 'esp_wifi_set_inactive_time'), INACTIVE_SET)
    code += '\n#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#include <stdlib.h>\n'
    code += '#define CONFIG_ESP32_MQUICKJS_WIFI_RADIO 1\n'
    code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_interval.h')
    code += unit(COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_interval.c')
    code += scan_support(radio)
    if profile.startswith('esp32c5/'):
        from tests.c.integration.wifi.twt.test_wifi_twt_controls import policy_support
        code += policy_support(radio)
    if he_statistics:
        from tests.c.integration.wifi.config.test_wifi_he_statistics import he_support
        code += he_support(radio)
    code += structure(radio, 'wifi_radio_restart_configs_t')
    code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_stop_snapshot_step_t;', header).group(0)
    code += structure(header, 'esp32_mquickjs_wifi_radio_stop_snapshot_t')
    code += '\nstatic esp32_mquickjs_wifi_radio_stop_snapshot_t s_stop_snapshot;\n'
    code += re.search(r'static struct \{[^}]*\} s_config_restart;', radio).group(0)
    code += re.search(r'enum \{ RESTART_CONFIG_RAM,[^}]*\};', radio).group(0)
    code += re.search(r'enum \{ RESTART_CAPTURE_RAM,[^}]*\};', radio).group(0)
    code += re.search(r'enum \{ RESTART_START_CHANNEL,[^}]*\};', radio).group(0)
    code += re.search(r'enum \{ RADIO_EVENTS_IDLE[^}]*\};', radio).group(0)
    code += structure((COMPONENT / 'internal/esp32_mquickjs_wifi_action_sdk.h').read_text(), 'esp32_mquickjs_wifi_saved_phy_t')
    code += BOUNDARIES + RECOVERY_BOUNDARIES
    code += fixture_text('wifi/lifecycle/test_wifi_restart_configs/config_code.inc')
    # No Vendor IE is present in this configuration/restart fixture.
    code += "static struct {esp32_mquickjs_wifi_radio_lifecycle_t start_owner;} s_vendor_ie;\n"
    if mutation_boundary:
        code += extract(radio, 'wifi_radio_invalidate_stop_snapshot_locked')
        code += (COMPONENT / 'internal/esp32_mquickjs_wifi_radio_mutation.h').read_text()
    code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
    for name in ('wifi_radio_set_state', 'wifi_radio_restore_inactive_locked', 'wifi_radio_start_stopped_locked', 'wifi_radio_validate_protocol', 'wifi_radio_read_phy', 'wifi_radio_write_phy', 'wifi_radio_phy_equal',
                 'esp32_mquickjs_wifi_radio_5ghz_channel_bit', 'wifi_radio_band_snapshot',
                 'wifi_radio_capture_stop_snapshot_locked', 'wifi_radio_stop_snapshot_unchanged_locked',
                 'wifi_radio_restart_stopped_observations_locked',
                 'wifi_radio_restart_phy_mode', 'wifi_radio_restart_band_mode_read', 'wifi_radio_restart_phy_subset_io', 'wifi_radio_restart_recovery_phy_read', 'wifi_radio_restart_phy_io', 'wifi_radio_restart_phy_read',
                 'wifi_radio_write_tx_rate', 'wifi_radio_country_valid', 'wifi_radio_country_equal', 'wifi_radio_config_equal', 'wifi_radio_restart_configs_owner', 'wifi_radio_restart_configs_record',
                 'wifi_radio_restart_configs_discard_locked', 'wifi_radio_restart_band_select_locked', 'wifi_radio_restart_band_prepare_locked', 'wifi_radio_restart_globals_read',
                 'wifi_radio_restart_mac_temporary', 'wifi_radio_restart_channel_read', 'wifi_radio_restart_recovery_home_read', 'wifi_radio_restart_inactive_read',
                 'wifi_radio_restart_capture_continue_locked', 'wifi_radio_restart_configs_capture_locked',
                 'esp32_mquickjs_wifi_radio_pmf_disable_allowed', 'wifi_radio_restore_disabled_pmf',
                 'wifi_radio_restart_config_io', 'wifi_radio_restart_configs_replay_locked',
                 'wifi_radio_restart_configs_ready_locked', 'wifi_radio_restart_configs_pre_start_locked',
                 'wifi_radio_restart_configs_post_start_locked',
                 'wifi_radio_restart_configs_verify_locked', 'wifi_radio_restart_configs_commit_storage_locked', 'wifi_radio_restart_configs_commit_locked',
                 'esp32_mquickjs_wifi_radio_restart_snapshot_bytes', 'esp32_mquickjs_wifi_radio_finish_lifecycle'):
        code += extract(radio, name)
    return code


class WiFiRestartConfigs(unittest.TestCase):
    def test_matching_ap_default_is_preserved_without_normalizing_write(self):
        code = config_code('esp32c5/representative', True)
        # The actual C5 default OPEN AP reports capable=true, while reapplying
        # that same configuration through set_config returns capable=false.
        code = code.replace('native_config[iface]=*input;', '''native_config[iface]=*input;
    if(iface==WIFI_IF_AP && input->ap.authmode==WIFI_AUTH_OPEN)
        native_config[iface].ap.pmf_cfg.capable=false;''')
        code += MAIN[:MAIN.index('static void disabled_pmf_replay')]
        compile_run(self, code + fixture_text('wifi/lifecycle/test_wifi_restart_configs/test_matching_ap_default_is_preserved_without_normalizing_write.inc'))

    def test_mac_replay_selects_stopped_interfaces_before_sdk_write(self):
        for ap in (False, True):
            with self.subTest(softap=ap):
                code = config_code('esp32c5/representative', ap)
                code += MAIN[:MAIN.index('static void disabled_pmf_replay')]
                compile_run(self, code + fixture_text('wifi/lifecycle/test_wifi_restart_configs/test_mac_replay_selects_stopped_interfaces_before_sdk_write.inc'))

    def test_sdk_byte_writes_do_not_leave_channel_enum_stack_bytes(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        code = config_code('esp32c5/representative', True)
        # The pinned C5 SDK's wifi_get_{home_,}channel_process stores just one
        # byte through the public wifi_second_chan_t pointer. Poison automatic
        # storage so the production helpers cannot accidentally rely on zeroes.
        for value in ('native_secondary', 'native_home_secondary'):
            original = '*secondary=' + value + ';'
            self.assertIn(original, code)
            code = code.replace(original, '*(uint8_t *)secondary=(uint8_t)' + value + ';')
        code += extract(radio, 'wifi_radio_restart_ap_channel_matches')
        code += MAIN[:MAIN.index('static void disabled_pmf_replay')]
        compile_run(self, code + BYTE_CHANNEL_MAIN,
                    cflags=('-ftrivial-auto-var-init=pattern',))

    def test_capture_retry_preserves_fault_and_defers_settings_until_rebuild(self):
        for ap in (False, True):
            with self.subTest(softap=ap):
                code = config_code('esp32c5/representative', ap)
                code = code.replace('static wifi_twt_config_t native_twt_policy;',
                                    'static wifi_twt_config_t native_twt_policy;static unsigned policy_writes;')
                code = code.replace('int error=sdk_step(write);if(error)return error;',
                                    'if(write)++policy_writes;int error=sdk_step(write);if(error)return error;')
                helpers = MAIN[:MAIN.index('static void disabled_pmf_replay')]
                compile_run(self, code + helpers + CAPTURE_RETRY_MAIN)

    def test_real_capture_oom_sdk_suffixes_ram_only_replay_exact_tokens_and_secret_wipe(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, config_code(profile, ap) + MAIN)


BYTE_CHANNEL_MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_configs/byte_channel_main.inc')

INACTIVE_GET = fixture_text('wifi/lifecycle/test_wifi_restart_configs/inactive_get.inc')
INACTIVE_SET = fixture_text('wifi/lifecycle/test_wifi_restart_configs/inactive_set.inc')

BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_restart_configs/boundaries.inc')

MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_configs/main.inc')

# Radio recovery admission and the pinned SDK task are independently covered by
# test_wifi_action_recovery and test_wifi_saved_phy_sdk. Default false retains
# all ordinary restart cases; the recovery checkpoint fixture enables it.
RECOVERY_BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_restart_configs/recovery_boundaries.inc')

CAPTURE_RETRY_MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_configs/capture_retry_main.inc')
