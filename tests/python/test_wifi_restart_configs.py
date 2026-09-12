"""Deferred production credential/global checkpoint; injected SDK/heap, no device proof."""
import re
import unittest
from test_wifi_connection_controls import control_code
from test_wifi_scan_parameters import scan_support
from test_wifi_config_controls import structure
from test_wifi_rx_target import unit
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, extract


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
        from test_wifi_twt_controls import policy_support
        code += policy_support(radio)
    if he_statistics:
        from test_wifi_he_statistics import he_support
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
    code += '''
/* No managed TWT generation participates in this configuration fixture. */
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
static struct {unsigned unused;} s_twt_recovery;
static bool wifi_radio_twt_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return false;}
static bool wifi_radio_twt_recovery_owners_locked(void) {return false;}
#endif
'''
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
        compile_run(self, code + r'''
int main(void) {
    setup();wifi_radio_operation_lock();new_driver();
    native_mode=s_radio.effective_mode=WIFI_MODE_APSTA;
    native_config[1].ap.authmode=WIFI_AUTH_OPEN;
    native_config[1].ap.pmf_cfg.capable=true;
    memcpy(native_config[1].ap.ssid,"ESP_default",11);
    native_config[1].ap.ssid_len=11;
    wifi_radio_restart_configs_t snapshot={0};
    snapshot.saved[1]=native_config[1];
    wifi_config_t frozen=snapshot.saved[1];
    unsigned before=writes;
    assert(wifi_radio_restart_config_io(&snapshot,1,true)==ESP_OK);
    assert(wifi_radio_restart_config_io(&snapshot,1,false)==ESP_OK);
    assert(writes==before && native_config[1].ap.pmf_cfg.capable);
    assert(!memcmp(&frozen,&snapshot.saved[1],sizeof(frozen)));
    wifi_config_t zero={0};
    assert(!memcmp(&zero,&snapshot.scratch,sizeof(zero)));
    /* A failed initial read must not proceed with a speculative write. */
    fail_at=calls+1;
    assert(wifi_radio_restart_config_io(&snapshot,1,true)==77);
    assert(writes==before && !memcmp(&zero,&snapshot.scratch,sizeof(zero)));
    fail_at=0;
    /* A different secured config must still be applied and checked exactly. */
    snapshot.saved[1].ap.authmode=WIFI_AUTH_WPA2_PSK;
    snapshot.saved[1].ap.pmf_cfg.required=true;
    memcpy(snapshot.saved[1].ap.password,"test-secret",11);
    assert(wifi_radio_restart_config_io(&snapshot,1,true)==ESP_OK);
    assert(writes>before);
    assert(wifi_radio_restart_config_io(&snapshot,1,false)==ESP_OK);
    native_config[1].ap.pmf_cfg.capable=false;
    assert(wifi_radio_restart_config_io(&snapshot,1,false)==ESP_ERR_INVALID_RESPONSE);
    native_config[1]=snapshot.saved[1];native_config[1].ap.password[0]^=1;
    assert(wifi_radio_restart_config_io(&snapshot,1,false)==ESP_ERR_INVALID_RESPONSE);
    assert(!nvs_writes && !memcmp(&zero,&snapshot.scratch,sizeof(zero)));
    wifi_radio_operation_unlock();return 0;
}
''')

    def test_mac_replay_selects_stopped_interfaces_before_sdk_write(self):
        for ap in (False, True):
            with self.subTest(softap=ap):
                code = config_code('esp32c5/representative', ap)
                code += MAIN[:MAIN.index('static void disabled_pmf_replay')]
                compile_run(self, code + r'''
int main(void) {
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    uint8_t saved[2][6];memcpy(saved,native_mac,sizeof(saved));
    new_driver();
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(!s_radio.started && !native_running && !native_starts);
    assert(!memcmp(saved[0],native_mac[0],6));
    if(CONFIG_ESP_WIFI_SOFTAP_SUPPORT)assert(!memcmp(saved[1],native_mac[1],6));
    assert(!nvs_writes);
    wipe();wifi_radio_operation_unlock();return 0;
}
''')

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


BYTE_CHANNEL_MAIN = r'''
int main(void) {
    for (unsigned second=0;second<=2;++second) {
        setup();wifi_radio_operation_lock();native_channel=native_home=6;
        native_secondary=native_home_secondary=(wifi_second_chan_t)second;
        wifi_radio_restart_configs_t snapshot={0};bool matches=false;
        assert(wifi_radio_restart_channel_read(&snapshot,false)==ESP_OK);
        assert(snapshot.primary==6 && snapshot.secondary==second);
        assert(wifi_radio_restart_channel_read(&snapshot,true)==ESP_OK);
        assert(wifi_radio_restart_recovery_home_read(&snapshot,true)==ESP_OK);
        assert(wifi_radio_restart_ap_channel_matches(&snapshot,&matches)==ESP_OK && matches);
        wifi_radio_capture_stop_snapshot_locked();
        assert(s_stop_snapshot.error==ESP_OK && s_stop_snapshot.unchanged);
        assert(s_stop_snapshot.secondary==second);
        native_home_secondary=(wifi_second_chan_t)((second+1)%3);
        assert(wifi_radio_restart_channel_read(&snapshot,false)==ESP_ERR_INVALID_STATE);
        assert(wifi_radio_restart_ap_channel_matches(&snapshot,&matches)==ESP_OK && !matches);
        native_secondary=native_home_secondary=(wifi_second_chan_t)99;
        assert(wifi_radio_restart_channel_read(&snapshot,false)==ESP_ERR_INVALID_RESPONSE);
        assert(wifi_radio_restart_recovery_home_read(&snapshot,false)==ESP_ERR_INVALID_RESPONSE);
        fail_at=calls+1;
        assert(wifi_radio_restart_channel_read(&snapshot,false)==77);
        wifi_radio_operation_unlock();
    }
    return 0;
}
'''

INACTIVE_GET = r'''
static wifi_mode_t native_mode;
static unsigned inactive_reads[2],inactive_writes[2],inactive_nvs_writes;
static bool corrupt_inactive;
static int esp_wifi_get_inactive_time(wifi_interface_t iface,uint16_t *value) {
    assert(s_radio.started && (native_mode & (1U<<iface)));
    ++inactive_reads[iface];*value=inactive[iface];
    if(corrupt_inactive && inactive_writes[iface])*value+=1;
    return sdk_step(false);
}
'''
INACTIVE_SET = r'''
static int esp_wifi_set_inactive_time(wifi_interface_t iface,uint16_t value) {
    assert(s_radio.started && (native_mode & (1U<<iface)));
    ++inactive_writes[iface];inactive[iface]=value;
    if(s_radio.storage==WIFI_STORAGE_FLASH)++inactive_nvs_writes;
    return sdk_step(true);
}
'''

BOUNDARIES = r'''
#define ESP_ERR_NO_MEM 17
#define WIFI_RADIO_SMARTCONFIG_PENDING false
#define WIFI_RADIO_WPS_PENDING false
#define WIFI_RADIO_DPP_PENDING false
#define WIFI_RADIO_EAP_PENDING false
#define WIFI_RADIO_NAN_PENDING false
#define WIFI_RADIO_MESH_PENDING false
static unsigned native_starts,native_stops;
static bool native_running,band_cycle_pending;
static int wifi_radio_begin_events(int phase,wifi_mode_t mode) {
    assert((phase==RADIO_EVENTS_START && mode!=WIFI_MODE_NULL && mode==native_mode && !s_radio.started) ||
        (phase==RADIO_EVENTS_RESTART && mode==WIFI_MODE_STA && s_radio.started && !band_cycle_pending));
    return sdk_step(false);
}
static int wifi_radio_wait_events(void) {
    assert(s_radio.started && s_radio.stop_required);int err=sdk_step(false);
    if(err==ESP_OK)band_cycle_pending=false;return err;
}
static int esp_wifi_start(void);
#define WIFI_PROTOCOL_11B 1
#define WIFI_PROTOCOL_11G 2
#define WIFI_PROTOCOL_11N 4
#define WIFI_PROTOCOL_LR 8
#define WIFI_PROTOCOL_11A 16
#define WIFI_PROTOCOL_11AC 32
#define WIFI_PROTOCOL_11AX 64
static wifi_band_mode_t native_band_mode;
static wifi_band_t native_band;
static uint8_t native_channel,native_home;
static wifi_second_chan_t native_secondary,native_home_secondary;
static unsigned band_writes,channel_writes,phy_reads[2];
static bool change_visible_phy,ignore_channel;
static int esp_wifi_get_band(wifi_band_t *out) {*out=native_band;return sdk_step(false);}
static int esp_wifi_get_channel(uint8_t *out,wifi_second_chan_t *secondary) {
    *out=native_channel;*secondary=native_secondary;return sdk_step(false);
}
static int esp_wifi_get_home_channel(uint8_t *out,wifi_second_chan_t *secondary) {
    *out=native_home;*secondary=native_home_secondary;return sdk_step(false);
}
static wifi_protocols_t native_protocols[2];
static wifi_bandwidths_t native_bandwidths[2];
static int esp_wifi_get_band_mode(wifi_band_mode_t *mode) {*mode=native_band_mode;return sdk_step(false);}
#if CONFIG_SOC_WIFI_SUPPORT_5G
static int esp_wifi_get_protocols(wifi_interface_t iface,wifi_protocols_t *out) {
    ++phy_reads[iface];*out=native_protocols[iface];
    if(native_band_mode==WIFI_BAND_MODE_2G_ONLY)out->ghz_5g=0;
    if(native_band_mode==WIFI_BAND_MODE_5G_ONLY)out->ghz_2g=0;
    return sdk_step(false);
}
static int esp_wifi_get_bandwidths(wifi_interface_t iface,wifi_bandwidths_t *out) {
    *out=native_bandwidths[iface];
    if(native_band_mode==WIFI_BAND_MODE_2G_ONLY)out->ghz_5g=0;
    if(native_band_mode==WIFI_BAND_MODE_5G_ONLY)out->ghz_2g=0;
    return sdk_step(false);
}
static int esp_wifi_set_protocols(wifi_interface_t iface,wifi_protocols_t *input) {
    assert(!s_radio.started && native_band_mode==WIFI_BAND_MODE_AUTO);
    assert(native_bandwidths[iface].ghz_2g==WIFI_BW20 && native_bandwidths[iface].ghz_5g==WIFI_BW20);
    native_protocols[iface]=*input;input->ghz_2g=0;return sdk_step(true);
}
static int esp_wifi_set_bandwidths(wifi_interface_t iface,wifi_bandwidths_t *input) {
    assert(!s_radio.started && native_band_mode==WIFI_BAND_MODE_AUTO);
    native_bandwidths[iface]=*input;input->ghz_2g=(wifi_bandwidth_t)99;return sdk_step(true);
}
#else
static int esp_wifi_get_protocol(wifi_interface_t iface,uint8_t *out) {*out=native_protocols[iface].ghz_2g;return sdk_step(false);}
static int esp_wifi_get_bandwidth(wifi_interface_t iface,wifi_bandwidth_t *out) {*out=native_bandwidths[iface].ghz_2g;return sdk_step(false);}
static int esp_wifi_set_protocol(wifi_interface_t iface,uint8_t input) {
    assert(!s_radio.started && native_bandwidths[iface].ghz_2g==WIFI_BW20);
    native_protocols[iface].ghz_2g=input;return sdk_step(true);
}
static int esp_wifi_set_bandwidth(wifi_interface_t iface,wifi_bandwidth_t input) {
    assert(!s_radio.started);native_bandwidths[iface].ghz_2g=input;return sdk_step(true);
}
#endif
#define WIFI_EVENT_MASK_AP_PROBEREQRECVED 1U
static wifi_country_t native_country;
static wifi_ps_type_t native_ps;
static uint32_t native_event_mask;
static uint8_t native_mac[2][6];
static const uint8_t factory_mac[2][6]={{2,0,0,0,0,11},{2,0,0,0,0,12}};
static unsigned mac_writes,power_writes,rate_writes[2];
static esp32_mquickjs_wifi_tx_rate_state_t s_tx_rates;
static wifi_tx_rate_config_t native_rates[2];
static int esp_wifi_config_80211_tx(wifi_interface_t iface,wifi_tx_rate_config_t *config) {
    assert(!s_radio.started && (iface==WIFI_IF_STA || (CONFIG_ESP_WIFI_SOFTAP_SUPPORT && iface==WIFI_IF_AP)));
    native_rates[iface]=*config;++rate_writes[iface];
    config->rate=WIFI_PHY_RATE_1M_L; /* Production SDK boundary preserves frozen and accepted input. */
    return sdk_step(true);
}
static esp32_mquickjs_wifi_interval_state_t s_interval;
static uint16_t native_interval;
static int wifi_radio_interval_writer(void *opaque,uint16_t value) {
    (void)opaque;native_interval=value;return sdk_step(true);
}
static int8_t native_tx_power;
static bool clamp_tx_power;
static int esp_wifi_get_max_tx_power(int8_t *output) {
    int err=sdk_step(false);if(err!=ESP_OK)return err;
    if(!s_radio.started)return ESP_ERR_WIFI_NOT_STARTED;
    *output=native_tx_power;return ESP_OK;
}
static int esp_wifi_set_max_tx_power(int8_t input) {
    assert(s_radio.started && !critical);++power_writes;
    native_tx_power=clamp_tx_power ? input-1 : input;return sdk_step(true);
}
static int esp_wifi_get_country(wifi_country_t *output) {*output=native_country;return sdk_step(false);}
static int esp_wifi_set_country(const wifi_country_t *input) {
    assert(!s_radio.started);int8_t maximum=native_country.max_tx_power;
    native_country=*input;native_country.max_tx_power=maximum;return sdk_step(true);
}
static int esp_wifi_get_ps(wifi_ps_type_t *output) {*output=native_ps;return sdk_step(false);}
static int esp_wifi_set_ps(wifi_ps_type_t input) {assert(!s_radio.started);native_ps=input;return sdk_step(true);}
static int esp_wifi_get_event_mask(uint32_t *output) {*output=native_event_mask;return sdk_step(false);}
static int esp_wifi_set_event_mask(uint32_t input) {
    assert(!s_radio.started && !(input & ~WIFI_EVENT_MASK_AP_PROBEREQRECVED));
    native_event_mask=input;return sdk_step(true);
}
static int esp_wifi_get_mac(wifi_interface_t iface,uint8_t *output) {
    assert(iface==WIFI_IF_STA || (CONFIG_ESP_WIFI_SOFTAP_SUPPORT && iface==WIFI_IF_AP));
    memcpy(output,native_mac[iface],6);return sdk_step(false);
}

static struct {esp32_mquickjs_wifi_radio_lifecycle_t owner;} s_policy_restart;
static wifi_config_t native_config[2];
static bool reenable_pmf;
static wifi_storage_t native_storage;
static int esp_wifi_set_channel(uint8_t primary,wifi_second_chan_t secondary) {
    assert(s_radio.started && native_mode==WIFI_MODE_STA && !band_cycle_pending);
    ++channel_writes;
    if(!ignore_channel) {
        native_channel=native_home=primary;native_secondary=native_home_secondary=secondary;
        native_band=primary>14 ? WIFI_BAND_5G : WIFI_BAND_2G;
    }
    return sdk_step(true);
}
#if CONFIG_SOC_WIFI_SUPPORT_5G
static int esp_wifi_set_band_mode(wifi_band_mode_t mode) {
    assert(s_radio.started && s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_STARTING && native_running);
    assert((mode==WIFI_BAND_MODE_AUTO || mode==WIFI_BAND_MODE_2G_ONLY || mode==WIFI_BAND_MODE_5G_ONLY) && native_mode==WIFI_MODE_STA);
    assert(native_storage==WIFI_STORAGE_RAM);
    native_band_mode=mode;band_cycle_pending=true;++band_writes;
    native_band=mode==WIFI_BAND_MODE_5G_ONLY ? WIFI_BAND_5G : WIFI_BAND_2G;
    native_channel=native_home=mode==WIFI_BAND_MODE_5G_ONLY ? 36 : 1;
    if(change_visible_phy)native_protocols[0].ghz_2g=WIFI_PROTOCOL_11B;
    return sdk_step(true);
}
#endif
static int esp_wifi_set_mac(wifi_interface_t iface,const uint8_t *input) {
    assert(!s_radio.started && !(input[0]&1U));
    /* Fixed SDK wifi_set_mac_process requires the selected mode to contain
     * the interface, even while its physical START flag is clear. */
    if(!(native_mode & (1U<<iface)))return 0x3005; /* ESP_ERR_WIFI_MODE */
    assert(iface==WIFI_IF_STA || (CONFIG_ESP_WIFI_SOFTAP_SUPPORT && iface==WIFI_IF_AP));
    if(CONFIG_ESP_WIFI_SOFTAP_SUPPORT)assert(memcmp(input,native_mac[1-iface],6));
    memcpy(native_mac[iface],input,6);++mac_writes;return sdk_step(true);
}

static void *allocation;static size_t allocation_size;static unsigned allocations,frees,nvs_writes;
static bool allocation_fail,mutate_input,corrupt_config;
static int cleanup_error;
static int esp_wifi_start(void) {
    assert((native_mode==WIFI_MODE_STA || (CONFIG_ESP_WIFI_SOFTAP_SUPPORT && (native_mode & WIFI_MODE_AP))) && s_radio.lifecycle.identity==41 && allocation);
    if((native_mode & WIFI_MODE_AP) && native_storage==WIFI_STORAGE_FLASH)++nvs_writes;
    for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)assert(!s_radio.leases[i].identity);
    native_running=true;++native_starts;return sdk_step(true);
}

static void *checkpoint_calloc(size_t n,size_t size) {
    assert(locks==1 && !critical && !allocation);++allocations;if(allocation_fail)return NULL;
    allocation_size=n*size;allocation=calloc(n,size);assert(allocation);return allocation;
}
static void checkpoint_free(void *p) {
    assert(p==allocation && !critical);const unsigned char *bytes=p;
    for(size_t i=0;i<allocation_size;++i)assert(bytes[i]==0); /* Every retained secret was wiped. */
    ++frees;free(p);allocation=NULL;
}
#define calloc checkpoint_calloc
#define free checkpoint_free
static int esp_wifi_get_mode(wifi_mode_t *mode) {*mode=native_mode;return sdk_step(false);}
static int esp_wifi_set_mode(wifi_mode_t mode) {assert(!s_radio.started);native_mode=mode;return sdk_step(true);}
static int esp_wifi_set_storage(wifi_storage_t storage) {native_storage=storage;return sdk_step(true);}
static int esp_wifi_get_config(wifi_interface_t iface,wifi_config_t *output) {
    *output=native_config[iface];if(corrupt_config && iface==WIFI_IF_STA)output->sta.pmf_cfg.required=!output->sta.pmf_cfg.required;
    return sdk_step(false); /* Partial output can exist before SDK failure. */
}
static int esp_wifi_disable_pmf_config(wifi_interface_t iface) {
    assert(!s_radio.started);
    wifi_pmf_config_t *pmf=iface==WIFI_IF_STA ? &native_config[iface].sta.pmf_cfg : &native_config[iface].ap.pmf_cfg;
    pmf->capable=pmf->required=false;return sdk_step(true);
}
static int esp_wifi_set_config(wifi_interface_t iface,wifi_config_t *input) {
    assert(!s_radio.started && (native_mode & (iface==WIFI_IF_STA ? WIFI_MODE_STA : WIFI_MODE_AP)));
    if(native_storage==WIFI_STORAGE_FLASH)++nvs_writes;
    native_config[iface]=*input;
    if(reenable_pmf) {
        wifi_pmf_config_t *pmf=iface==WIFI_IF_STA ? &native_config[iface].sta.pmf_cfg : &native_config[iface].ap.pmf_cfg;
        pmf->capable=true; /* capable=false alone is not a disable request. */
    }
    if(mutate_input)input->sta.password[0]^=1; /* SDK cannot corrupt frozen original. */
    return sdk_step(true);
}
static int wifi_radio_shutdown_locked(void) {assert(locks==1);return cleanup_error;}
static int wifi_radio_stop_locked(void) {
    if(!s_radio.stop_required)return ESP_OK;
    assert(locks==1 && native_running && !band_cycle_pending);++native_stops;
    int err=cleanup_error ? cleanup_error : sdk_step(true);
    if(err==ESP_OK){native_running=false;s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;}
    return err;
}
'''

MAIN = r'''
static esp32_mquickjs_wifi_radio_lifecycle_t token={.generation=7,.identity=41};
static void setup(void) {
    assert(!allocation);reset();memset(&s_config_restart,0,sizeof(s_config_restart));
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    memset(&s_twt_policy,0,sizeof(s_twt_policy));native_twt_policy=(wifi_twt_config_t){0};
#endif
    memset(&s_stop_snapshot,0,sizeof(s_stop_snapshot));
    memset(&s_scan_parameters,0,sizeof(s_scan_parameters));
    native_scan=(wifi_scan_default_params_t)WIFI_SCAN_PARAMS_DEFAULT_CONFIG();
    scan_partial_failure=scan_corrupt=false;scan_writes=scan_resets=0;
    native_running=true;band_cycle_pending=false;native_starts=native_stops=0;
    memset(inactive_reads,0,sizeof(inactive_reads));memset(inactive_writes,0,sizeof(inactive_writes));
    inactive_nvs_writes=0;corrupt_inactive=false;inactive[0]=17;inactive[1]=601;
    s_radio.lifecycle.identity=41;s_radio.lifecycle.generation=7;
    s_radio.started=true;s_radio.stop_required=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    s_radio.effective_mode=native_mode=WIFI_MODE_STA;s_radio.storage=native_storage=WIFI_STORAGE_FLASH;
    memset(s_radio.leases,0,sizeof(s_radio.leases));memset(native_config,0,sizeof(native_config));
    memcpy(native_config[0].sta.ssid,"private-sta",11);memcpy(native_config[0].sta.password,"secret-sta",10);
    native_config[0].sta.pmf_cfg.required=true;
    memcpy(native_config[1].ap.ssid,"private-ap",10);memcpy(native_config[1].ap.password,"secret-ap",9);
    native_config[1].ap.ssid_len=10;native_config[1].ap.pmf_cfg.required=true;
    native_country=(wifi_country_t){.cc="US",.schan=1,.nchan=11,.max_tx_power=19,.policy=WIFI_COUNTRY_POLICY_MANUAL};
#if CONFIG_SOC_WIFI_SUPPORT_5G
    native_country.wifi_5g_channel_mask=1;
#endif
    native_ps=WIFI_PS_MAX_MODEM;native_event_mask=0;
    memcpy(native_mac[0],factory_mac[CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 1 : 0],6);
    memcpy(native_mac[1],factory_mac[0],6); /* Saved pair swapped relative to new-driver defaults. */
    native_tx_power=52;clamp_tx_power=false;
    native_band=WIFI_BAND_2G;native_channel=native_home=1;
    native_secondary=native_home_secondary=WIFI_SECOND_CHAN_NONE;
    band_writes=channel_writes=0;memset(phy_reads,0,sizeof(phy_reads));change_visible_phy=ignore_channel=false;
    native_band_mode=CONFIG_SOC_WIFI_SUPPORT_5G ? WIFI_BAND_MODE_AUTO : WIFI_BAND_MODE_2G_ONLY;
    for(unsigned i=0;i<2;++i) {
        native_protocols[i]=(wifi_protocols_t){.ghz_2g=WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N,.ghz_5g=WIFI_PROTOCOL_11A|WIFI_PROTOCOL_11N};
        native_bandwidths[i]=(wifi_bandwidths_t){.ghz_2g=i ? WIFI_BW20 : WIFI_BW40,.ghz_5g=WIFI_BW20};
    }
    memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));memset(&s_tx_rates,0,sizeof(s_tx_rates));
    memset(native_rates,0,sizeof(native_rates));memset(rate_writes,0,sizeof(rate_writes));
    for(unsigned i=0;i<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 2U : 1U);++i) {
        native_rates[i]=(wifi_tx_rate_config_t){.phymode=WIFI_PHY_MODE_11G,.rate=i ? WIFI_PHY_RATE_12M : WIFI_PHY_RATE_6M};
        s_tx_rates.records[i]=(esp32_mquickjs_wifi_tx_rate_record_t){.known=true,.generation=7,.write_identity=i+1,.config=native_rates[i]};
    }
    s_tx_rates.next_identity=CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3 : 2;
    s_interval=(esp32_mquickjs_wifi_interval_state_t){.generation=7,.revision=5,.value=300,.known=true};native_interval=300;
    allocations=frees=nvs_writes=mac_writes=power_writes=0;allocation_fail=mutate_input=corrupt_config=reenable_pmf=false;cleanup_error=0;
    token=(esp32_mquickjs_wifi_radio_lifecycle_t){.generation=7,.identity=41};
}
static void new_driver(void) {
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    memset(&s_twt_policy,0,sizeof(s_twt_policy));native_twt_policy=(wifi_twt_config_t){0};
#endif
    memset(&s_scan_parameters,0,sizeof(s_scan_parameters));
    native_scan=(wifi_scan_default_params_t)WIFI_SCAN_PARAMS_DEFAULT_CONFIG();
    scan_partial_failure=scan_corrupt=false;scan_writes=scan_resets=0;
    memset(&s_inactive_history,0,sizeof(s_inactive_history)); /* Proven physical deinit boundary. */
    esp32_mquickjs_wifi_tx_rate_invalidate(&s_tx_rates);
    memset(native_rates,0,sizeof(native_rates));memset(rate_writes,0,sizeof(rate_writes));
    assert(esp32_mquickjs_wifi_interval_invalidate(&s_interval,s_radio.generation));
    ++s_radio.generation;memset(native_config,0,sizeof(native_config));
    native_running=band_cycle_pending=false;native_starts=native_stops=0;
    inactive[0]=6;inactive[1]=300;
    memset(inactive_reads,0,sizeof(inactive_reads));memset(inactive_writes,0,sizeof(inactive_writes));
    inactive_nvs_writes=0;corrupt_inactive=false;
    native_band=WIFI_BAND_2G;native_channel=native_home=1;
    native_secondary=native_home_secondary=WIFI_SECOND_CHAN_NONE;channel_writes=band_writes=0;
    native_band_mode=CONFIG_SOC_WIFI_SUPPORT_5G ? WIFI_BAND_MODE_AUTO : WIFI_BAND_MODE_2G_ONLY;
    for(unsigned i=0;i<2;++i) {
        native_protocols[i]=(wifi_protocols_t){.ghz_2g=WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G,.ghz_5g=WIFI_PROTOCOL_11A};
        native_bandwidths[i]=(wifi_bandwidths_t){.ghz_2g=WIFI_BW20,.ghz_5g=WIFI_BW20};
    }
    calls=writes=fail_at=0;
    esp32_mquickjs_wifi_interval_result_t baseline;
    assert(esp32_mquickjs_wifi_interval_write(&s_interval,s_radio.generation,0,wifi_radio_interval_writer,NULL,&baseline)==ESP_OK);
    native_country=(wifi_country_t){.cc="01",.schan=1,.nchan=11,.max_tx_power=20,.policy=WIFI_COUNTRY_POLICY_AUTO};
    native_ps=WIFI_PS_MIN_MODEM;native_event_mask=WIFI_EVENT_MASK_AP_PROBEREQRECVED;
    memcpy(native_mac,factory_mac,sizeof(native_mac));mac_writes=power_writes=0;native_tx_power=80;
    s_radio.storage=native_storage=WIFI_STORAGE_RAM;s_radio.effective_mode=native_mode=WIFI_MODE_STA;
    s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.fault_stage=NULL;s_radio.cleanup_stage=NULL;s_radio.fault_error=0;
    s_radio.driver_owned=s_radio.storage_configured=true;calls=writes=fail_at=0;
}
static void wipe(void) {wifi_radio_restart_configs_discard_locked(&token);assert(!allocation && !s_config_restart.captured);}
static void disabled_pmf_replay(void) {
    setup();wifi_radio_operation_lock();
    native_config[0].sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
    native_config[0].sta.disable_wpa3_compatible_mode=true;
    native_config[0].sta.pmf_cfg=(wifi_pmf_config_t){0};
    native_config[1].ap.authmode=WIFI_AUTH_WPA2_PSK;
    native_config[1].ap.pmf_cfg=(wifi_pmf_config_t){0};
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
    new_driver();reenable_pmf=true;
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(!native_config[0].sta.pmf_cfg.capable && !native_config[0].sta.pmf_cfg.required);
    if(CONFIG_ESP_WIFI_SOFTAP_SUPPORT)assert(!native_config[1].ap.pmf_cfg.capable && !native_config[1].ap.pmf_cfg.required);
    assert(!nvs_writes && !memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
    assert(wifi_radio_restart_configs_pre_start_locked(&token,WIFI_MODE_STA)==ESP_OK);
    wipe();wifi_radio_operation_unlock();
}
static void pre_start_cases(void) {
    const wifi_mode_t modes[]={WIFI_MODE_STA,WIFI_MODE_AP,WIFI_MODE_APSTA};
    for(unsigned mi=0;mi<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3U : 1U);++mi) {
        for(unsigned scenario=0;scenario<7;++scenario) {
            setup();wifi_radio_operation_lock();native_config[1].ap.channel=1;
            s_radio.effective_mode=native_mode=modes[mi];
            if(mi && scenario==5)native_config[1].ap.channel=6; /* Legitimate former STA-following mismatch. */
            if(mi && scenario==6)native_config[1].ap.channel=0; /* SDK selects startup; CSA restores the frozen actual channel. */
            assert(wifi_radio_restart_configs_capture_locked(&token,modes[mi])==ESP_OK);
            new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
            wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
            unsigned before=calls,write_count=writes,start_count=native_starts;
            if(scenario==1)native_config[0].sta.pmf_cfg.required=false;
            if(scenario==2 && CONFIG_ESP_WIFI_SOFTAP_SUPPORT)native_config[1].ap.password[0]^=1;
            if(scenario==3)fail_at=calls+1;
            if(scenario==4)fail_at=calls+(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 2 : 1);
            esp_err_t expected=(scenario==3 || scenario==4) ? 77 :
                (scenario==1 || (scenario==2 && CONFIG_ESP_WIFI_SOFTAP_SUPPORT)) ? ESP_ERR_INVALID_RESPONSE :
                ESP_OK;
            assert(wifi_radio_restart_configs_pre_start_locked(&token,modes[mi])==expected);
            assert(calls>before && writes==write_count && native_starts==start_count && !s_radio.started);
            assert(!memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)) && allocation && !frees);
            if(expected!=ESP_OK) {
                assert(s_radio.fault_stage && s_radio.configuration.error==expected);
                before=calls;assert(wifi_radio_restart_configs_pre_start_locked(&token,modes[mi])==ESP_ERR_INVALID_STATE && calls==before);
            }
            wipe();wifi_radio_operation_unlock();
        }
    }
    for(unsigned bad=0;bad<8;++bad) {
        setup();wifi_radio_operation_lock();
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        esp32_mquickjs_wifi_radio_lifecycle_t request=token;
        switch(bad) {
            case 0:request.identity++;break;
            case 1:request.generation++;break;
            case 2:s_config_restart.replay_generation--;break;
            case 3:s_radio.leases[0].identity=99;break;
            case 4:s_radio.operation.identity=1;break;
            case 5:s_radio.wake_locks=1;break;
            case 6:s_radio.started=true;break;
            case 7:s_radio.cleanup_stage="pending";break;
        }
        unsigned before=calls;
        assert(wifi_radio_restart_configs_pre_start_locked(&request,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && calls==before);
        wipe();wifi_radio_operation_unlock();
    }
}
static void restore_cases(void) {
    for(unsigned failure=1;failure<=6;++failure) {
        setup();wifi_radio_operation_lock();native_channel=native_home=6;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        if(failure<=5)fail_at=calls+failure;else ignore_channel=true;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==(failure<=5 ? 77 : ESP_ERR_INVALID_RESPONSE));
        assert(channel_writes==1 && !power_writes && allocation && !memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
        assert(s_config_restart.start_phase==(failure==1 ? RESTART_START_CHANNEL : RESTART_START_CHANNEL_READ));
        unsigned before=calls;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_ERR_INVALID_STATE && calls==before);
        new_driver();ignore_channel=false;
        assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK && channel_writes==1 && native_channel==6 && native_home==6);
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wipe();wifi_radio_operation_unlock();
    }
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    for(unsigned mode=WIFI_MODE_AP;mode<=WIFI_MODE_APSTA;++mode) {
        setup();wifi_radio_operation_lock();native_channel=native_home=6;
        s_radio.effective_mode=native_mode=(wifi_mode_t)mode;
        assert(wifi_radio_restart_configs_capture_locked(&token,(wifi_mode_t)mode)==ESP_OK);
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_ERR_INVALID_RESPONSE);
        assert(!channel_writes && !power_writes && !strcmp(s_radio.configuration.stage,"restart-channel-readback"));
        assert(allocation);wipe();wifi_radio_operation_unlock();
    }
#endif
#if CONFIG_SOC_WIFI_SUPPORT_5G
    /* AUTO also needs its original current band, rather than an assumed 2G default. */
    setup();wifi_radio_operation_lock();native_band=WIFI_BAND_5G;native_channel=native_home=44;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(!native_starts && !native_stops && !band_writes);
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    assert(native_band_mode==WIFI_BAND_MODE_AUTO && native_band==WIFI_BAND_5G && native_channel==44);
    assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);
    wipe();wifi_radio_operation_unlock();
    setup();wifi_radio_operation_lock();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    unsigned replay_calls=calls;assert(native_starts==1 && native_stops==1 && native_band_mode==WIFI_BAND_MODE_2G_ONLY);
    wipe();wifi_radio_operation_unlock();
    for(unsigned failure=1;failure<=replay_calls;++failure) {
        setup();wifi_radio_operation_lock();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
        new_driver();fail_at=failure;
        assert(wifi_radio_restart_configs_replay_locked(&token)==77 && allocation && !nvs_writes);
        assert(!memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
        for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)assert(!s_radio.leases[i].identity);
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        assert(native_starts==1 && native_stops==1 && !s_radio.started && !s_radio.stop_required);
        assert(native_band_mode==WIFI_BAND_MODE_2G_ONLY && !nvs_writes);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wipe();wifi_radio_operation_unlock();
    }
#endif
}
#if CONFIG_SOC_WIFI_SUPPORT_5G
static void capture_cases(void) {
    /* Calls production capture/continuation. SDK fixtures hide inactive PHY;
     * the injected preparation resets the current channel, as the SDK may. */
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    for(unsigned mode=WIFI_MODE_AP;mode<=WIFI_MODE_APSTA;++mode) {
        setup();wifi_radio_operation_lock();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
        s_radio.effective_mode=native_mode=(wifi_mode_t)mode;
        assert(wifi_radio_restart_configs_capture_locked(&token,(wifi_mode_t)mode)==ESP_OK);
        assert(s_config_restart.mode==(wifi_mode_t)mode && native_mode==WIFI_MODE_STA);
        assert(native_starts==1 && native_stops==1 && !nvs_writes);
        wipe();wifi_radio_operation_unlock();
    }
#endif
    for(unsigned band=0;band<2;++band) {
        setup();wifi_radio_operation_lock();
        native_band_mode=band ? WIFI_BAND_MODE_5G_ONLY : WIFI_BAND_MODE_2G_ONLY;
        native_band=band ? WIFI_BAND_5G : WIFI_BAND_2G;native_channel=native_home=band ? 36 : 6;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        assert(s_config_restart.captured && allocations==1 && !frees && !nvs_writes);
        assert(native_starts==1 && native_stops==1 && band_writes==1 && native_running);
        assert(s_config_restart.snapshot->band_mode==(band ? WIFI_BAND_MODE_5G_ONLY : WIFI_BAND_MODE_2G_ONLY));
        assert(s_config_restart.snapshot->primary==(band ? 36 : 6) && native_channel==1);
        assert(s_config_restart.snapshot->storage==WIFI_STORAGE_FLASH && native_storage==WIFI_STORAGE_RAM);
        assert(s_config_restart.snapshot->protocols[0].ghz_2g && s_config_restart.snapshot->protocols[0].ghz_5g);
        for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)assert(!s_radio.leases[i].identity);
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
        assert(native_band_mode==s_config_restart.snapshot->band_mode && native_channel==s_config_restart.snapshot->primary);
        assert(channel_writes==1 && native_starts==1 && native_stops==1);
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);
        native_home=native_channel==1 ? 6 : 1;
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
        assert(!strcmp(s_radio.configuration.stage,"restart-band-channel-final-readback") && allocation);
        wipe();wifi_radio_operation_unlock();
    }
    setup();wifi_radio_operation_lock();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    unsigned capture_calls=calls;wipe();wifi_radio_operation_unlock();
    for(unsigned failure=1;failure<=capture_calls;++failure) {
        setup();wifi_radio_operation_lock();native_band_mode=WIFI_BAND_MODE_2G_ONLY;fail_at=failure;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==77);
        if(!allocation) {assert(!writes && frees==1);wifi_radio_operation_unlock();continue;}
        assert(!s_config_restart.captured && allocations==1 && !frees && !nvs_writes);
        assert(s_config_restart.snapshot->storage==WIFI_STORAGE_FLASH);
        assert(s_config_restart.snapshot->band_mode==WIFI_BAND_MODE_2G_ONLY);
        assert(!memcmp(s_config_restart.snapshot->saved[0].sta.password,"secret-sta",10));
        assert(!wifi_radio_restart_configs_ready_locked(&token,WIFI_MODE_STA));
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_ERR_INVALID_STATE);
        unsigned before=calls,starts=native_starts,band_changes=band_writes,sta_reads=phy_reads[0];
        uint8_t phase=s_config_restart.snapshot->capture_phase;
        esp32_mquickjs_wifi_radio_lifecycle_t stale=token;stale.identity++;
        assert(wifi_radio_restart_configs_capture_locked(&stale,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && calls==before);
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_AP)==ESP_ERR_INVALID_STATE && calls==before);
        ++s_radio.generation;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && calls==before);
        --s_radio.generation;fail_at=0;
        if(phase==RESTART_CAPTURE_FAILED) {
            assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==77 && calls==before);
        } else {
            assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK && allocations==1);
            if(phase>=RESTART_CAPTURE_START_EVENTS)assert(native_starts==starts);
            if(phase>=RESTART_CAPTURE_AUTO_READ)assert(band_writes==band_changes);
            if(phase==RESTART_CAPTURE_AP_PHY)assert(phy_reads[0]==sta_reads);
        }
        /* A failed explicit cleanup retains the same allocation and owner. */
        wifi_radio_operation_unlock();cleanup_error=77;
        assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==77 && allocation && token.identity==41);
        cleanup_error=0;assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==ESP_OK && !allocation && !token.identity);
    }
    setup();wifi_radio_operation_lock();native_band_mode=WIFI_BAND_MODE_2G_ONLY;change_visible_phy=true;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE);
    assert(allocation && !s_config_restart.captured && s_config_restart.snapshot->capture_phase==RESTART_CAPTURE_STA_PHY);
    assert(s_config_restart.snapshot->protocols[0].ghz_2g==(WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N));
    assert(s_config_restart.snapshot->protocols[0].ghz_5g==0); /* No partial assignment on comparison failure. */
    wipe();wifi_radio_operation_unlock();
    setup();wifi_radio_operation_lock();native_home=6;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(!allocation && !writes && frees==1);wifi_radio_operation_unlock();
}
#endif
int main(void) {
    disabled_pmf_replay();
    pre_start_cases();
    restore_cases();
#if CONFIG_SOC_WIFI_SUPPORT_5G
    capture_cases();
#endif
    setup();wifi_radio_operation_lock();allocation_fail=true;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_NO_MEM && !calls && !allocation && !s_config_restart.captured);
    wifi_radio_operation_unlock();
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    unsigned capture_calls=calls;wipe();wifi_radio_operation_unlock();
    for(unsigned failure=1;failure<=capture_calls;++failure) {
        setup();wifi_radio_operation_lock();fail_at=failure;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==77 && frees==1 && !allocation && !s_config_restart.captured && !writes);
        wifi_radio_operation_unlock();
    }
    /* Discover the SDK boundary count from a successful production replay. */
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    unsigned replay_calls=calls;assert(replay_calls>0);wipe();wifi_radio_operation_unlock();
    for(unsigned failure=1;failure<=replay_calls;++failure) {
        setup();wifi_radio_operation_lock();
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK && !writes);
        assert(s_config_restart.snapshot->mask==(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3 : 1));
        wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
        unsigned before=calls;
        assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_ERR_INVALID_STATE && calls==before); /* No physical transition. */
        new_driver();fail_at=failure;mutate_input=true;
        assert(wifi_radio_restart_configs_replay_locked(&token)==77 && allocation && !nvs_writes);
        assert(!memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)) && !wifi_radio_restart_configs_ready_locked(&token,WIFI_MODE_STA));
        before=calls;assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK && calls==before); /* Retain original, never failed output. */
        wifi_radio_operation_unlock();assert(esp32_mquickjs_wifi_radio_restart_snapshot_bytes()==sizeof(frozen));wifi_radio_operation_lock();
        new_driver();
        assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK && calls==replay_calls && !nvs_writes && native_storage==WIFI_STORAGE_RAM && native_mode==WIFI_MODE_STA);
        assert(rate_writes[0]==1 && native_rates[0].rate==WIFI_PHY_RATE_6M && s_tx_rates.records[0].known);
        if(CONFIG_ESP_WIFI_SOFTAP_SUPPORT)assert(rate_writes[1]==1 && native_rates[1].rate==WIFI_PHY_RATE_12M);
        assert(native_bandwidths[0].ghz_2g==WIFI_BW40 && native_protocols[0].ghz_2g==(WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N));
        assert(native_interval==300 && s_interval.known && !s_interval.uncertain);
        assert(mac_writes==(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3U : 1U));
        assert(!memcmp(native_mac[0],frozen.mac[0],6));
        if(CONFIG_ESP_WIFI_SOFTAP_SUPPORT)assert(!memcmp(native_mac[1],frozen.mac[1],6));
        assert(native_ps==WIFI_PS_MAX_MODEM && native_event_mask==0 && native_country.max_tx_power==20);
        assert(wifi_radio_country_equal(&native_country,&frozen.country,false));
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK && native_tx_power==52 && power_writes==1);
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);
        assert(!memcmp(native_config[0].sta.password,"secret-sta",10) && native_config[0].sta.pmf_cfg.required);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        assert(!memcmp(native_config[1].ap.password,"secret-ap",9) && native_config[1].ap.pmf_cfg.required);
#endif
        corrupt_config=true;
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE && allocation && !frees);
        for(size_t i=0;i<sizeof(frozen.scratch);++i)assert(((unsigned char *)&s_config_restart.snapshot->scratch)[i]==0);
        esp32_mquickjs_wifi_radio_lifecycle_t stale=token;stale.identity++;
        wifi_radio_restart_configs_discard_locked(&stale);assert(allocation);
        wifi_radio_operation_unlock();cleanup_error=77;
        assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==77 && allocation && token.identity==41);
        cleanup_error=0;assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==ESP_OK && !allocation && !token.identity && frees==1);
        assert(!esp32_mquickjs_wifi_radio_restart_snapshot_bytes());
    }
    for(unsigned invalid=0;invalid<6;++invalid) {
        if(invalid==4 && !CONFIG_ESP_WIFI_SOFTAP_SUPPORT)continue;
        setup();wifi_radio_operation_lock();
        if(invalid==0)native_country.nchan=0;
        if(invalid==1)native_ps=(wifi_ps_type_t)99;
        if(invalid==2)native_event_mask=2;
        if(invalid==3)native_mac[0][0]|=1;
        if(invalid==4)memcpy(native_mac[1],native_mac[0],6);
        if(invalid==5)native_tx_power=0;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE);
        assert(!writes && !allocation && frees==1);wifi_radio_operation_unlock();
    }
    for(unsigned invalid=0;invalid<6;++invalid) {
        if(invalid==4 && !CONFIG_ESP_WIFI_SOFTAP_SUPPORT)continue;
        setup();wifi_radio_operation_lock();
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
        if(invalid==0)native_country.nchan=10;
        if(invalid==1)native_ps=WIFI_PS_NONE;
        if(invalid==2)native_event_mask=1;
        if(invalid==3)native_mac[0][5]^=32;
        if(invalid==4)native_mac[1][5]^=32;
        if(invalid==5)native_tx_power=80;
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE);
        assert(allocation && !frees);wipe();wifi_radio_operation_unlock();
    }
    for(unsigned invalid=0;invalid<5;++invalid) {
        setup();wifi_radio_operation_lock();
        if(invalid==0)s_interval.known=false;
        if(invalid==1)s_interval.uncertain=true;
        if(invalid==2)s_interval.generation++;
        if(invalid==3){s_interval.owner.identity=41;s_interval.restore_pending=true;}
        if(invalid==4)s_interval.revision=UINT32_MAX-1;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
        assert(!calls && !writes && !allocation && frees==1);wifi_radio_operation_unlock();
    }
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    ++s_interval.revision; /* Same scalar without the accepted replay revision is insufficient. */
    assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && allocation);
    s_interval.revision=UINT32_MAX-1;unsigned before=calls;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && allocation && calls==before);
    wipe();wifi_radio_operation_unlock();
    for(unsigned invalid=0;invalid<5;++invalid) {
        setup();wifi_radio_operation_lock();
        if(invalid==0)s_tx_rates.records[0].known=false;
        if(invalid==1)s_tx_rates.records[0].uncertain=true;
        if(invalid==2)s_tx_rates.records[0].generation++;
        if(invalid==3)s_tx_rates.next_identity=0;
        if(invalid==4)s_tx_rate_lease.restore_pending=true;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
        assert(!calls && !writes && !allocation && frees==(invalid==4 ? 0U : 1U));wifi_radio_operation_unlock();
    }
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    ++s_tx_rates.records[0].write_identity;
    assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && allocation);
    s_tx_rates.next_identity=0;before=calls;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && calls==before && allocation);
    wipe();wifi_radio_operation_unlock();
    for(unsigned invalid=0;invalid<3;++invalid) {
        setup();wifi_radio_operation_lock();
        if(invalid==0)native_protocols[0].ghz_2g=0;
        if(invalid==1)native_bandwidths[0].ghz_2g=(wifi_bandwidth_t)99;
        if(invalid==2)native_band_mode=(wifi_band_mode_t)99;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==
            ESP_ERR_INVALID_RESPONSE);
        assert(!writes && !allocation && frees==1);wifi_radio_operation_unlock();
    }
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    native_bandwidths[0].ghz_2g=WIFI_BW20;
    assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE && allocation);
    wipe();wifi_radio_operation_unlock();
#if CONFIG_SOC_WIFI_SUPPORT_5G
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK && allocation);
    assert(native_starts==1 && native_stops==1 && !native_running && !s_radio.started && !s_radio.stop_required);
    assert(native_mode==WIFI_MODE_STA && native_band_mode==WIFI_BAND_MODE_AUTO);
    wipe();wifi_radio_operation_unlock();
#endif
#if CONFIG_SOC_WIFI_SUPPORT_5G
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
    const char *prepare_stage=NULL;
    assert(wifi_radio_restart_band_prepare_locked(&token,(wifi_mode_t)s_config_restart.snapshot->mask,&prepare_stage)==ESP_OK);
    unsigned prepare_calls=calls;assert(native_starts==1 && native_stops==1 && !native_running);
    wipe();wifi_radio_operation_unlock();
    for(unsigned failure=1;failure<=prepare_calls;++failure) {
        setup();wifi_radio_operation_lock();
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
        new_driver();native_band_mode=WIFI_BAND_MODE_5G_ONLY;fail_at=failure;
        assert(wifi_radio_restart_band_prepare_locked(&token,(wifi_mode_t)s_config_restart.snapshot->mask,&prepare_stage)==77);
        assert(allocation && token.identity==41 && !memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
        for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)assert(!s_radio.leases[i].identity);
        new_driver();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
        assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        assert(native_starts==1 && native_stops==1 && !native_running && !s_radio.stop_required);
        wipe();wifi_radio_operation_unlock();
    }
#endif
    /* Merely clearing started is not completed STOP: stop_required still rejects. */
    setup();wifi_radio_operation_lock();s_radio.started=false;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(!writes && !allocation && !allocations && !frees);wifi_radio_operation_unlock();
    for(unsigned failure=1;failure<=3;++failure) {
        setup();wifi_radio_operation_lock();
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_ERR_INVALID_STATE && !power_writes);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        if(failure<3)fail_at=calls+5+failure;else clamp_tx_power=true; /* Channel write + four getters precede power. */
        assert(wifi_radio_restart_configs_post_start_locked(&token)==(failure<3 ? 77 : ESP_ERR_INVALID_RESPONSE));
        assert(allocation && !frees && !memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
        assert(s_config_restart.start_phase==(failure==1 ? RESTART_START_POWER : RESTART_START_POWER_READ));
        unsigned before=calls;assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_ERR_INVALID_STATE && calls==before);
        new_driver();clamp_tx_power=false;
        assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK && s_config_restart.start_phase==RESTART_START_CHANNEL);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK && native_tx_power==52);
        before=calls;assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK && calls==before);
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wipe();wifi_radio_operation_unlock();
    }
    setup();wifi_radio_operation_lock();s_radio.started=s_radio.stop_required=native_running=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.driver_owned=s_radio.storage_configured=false;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK && !allocation && !allocations && !calls);
    s_radio.driver_owned=s_radio.storage_configured=true;
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK && wifi_radio_restart_configs_ready_locked(&token,WIFI_MODE_STA));
    assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);wipe();wifi_radio_operation_unlock();
    assert(!locks && !critical && !helper_locks);return 0;
}
'''

# Radio recovery admission and the pinned SDK task are independently covered by
# test_wifi_action_recovery and test_wifi_saved_phy_sdk. Default false retains
# all ordinary restart cases; the recovery checkpoint fixture enables it.
RECOVERY_BOUNDARIES = r'''
static bool raw_recovery_admitted;
static bool wifi_radio_raw_tx_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    return raw_recovery_admitted && token && token->identity==s_radio.lifecycle.identity && token->generation==s_radio.lifecycle.generation;
}
static uint32_t wifi_radio_raw_tx_recovery_capture_owner_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static bool recovery_admitted;
static uint32_t recovery_owner;
static uint32_t wifi_radio_raw_tx_recovery_capture_owner_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    return wifi_radio_raw_tx_recovery_exact_locked(token) ? recovery_owner : 0;
}
static unsigned saved_phy_reads;
static bool change_home_after_phy;
static bool wifi_radio_action_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    return recovery_admitted && token && token->identity==s_radio.lifecycle.identity && token->generation==s_radio.lifecycle.generation;
}
static uint32_t wifi_radio_action_recovery_capture_owner_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    return wifi_radio_action_recovery_exact_locked(token)?recovery_owner:0;
}
static int esp32_mquickjs_wifi_action_sdk_saved_phy(wifi_interface_t interface,esp32_mquickjs_wifi_saved_phy_t *output) {
    assert(locks && !critical && interface<=1);++saved_phy_reads;
    *output=(esp32_mquickjs_wifi_saved_phy_t){.protocols=native_protocols[interface],.bandwidths=native_bandwidths[interface]};
    if(change_home_after_phy)native_home=6;
    return sdk_step(false);
}
'''

CAPTURE_RETRY_MAIN = r'''
int main(void) {
    setup();wifi_radio_operation_lock();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
    native_twt_policy=(wifi_twt_config_t){true,false};
    s_twt_policy.value=native_twt_policy;s_twt_policy.known=true;
    cleanup_error=77;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==77);
    assert(allocation && !s_config_restart.captured && s_config_restart.snapshot->capture_phase==RESTART_CAPTURE_STOP);
    const char *original_fault=s_radio.fault_stage;
    assert(original_fault && s_radio.fault_error==77 && !policy_writes);
    cleanup_error=0;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(s_config_restart.captured && s_radio.fault_stage==original_fault && s_radio.fault_error==77);
    assert(!policy_writes && s_twt_policy.pending && s_scan_parameters.pending);
    assert(s_config_restart.snapshot->twt_policy.post_wakeup_event);
    assert(!s_config_restart.snapshot->twt_policy.twt_enable_keep_alive);
    unsigned starts=native_starts,stops=native_stops,reads=calls;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(native_starts==starts && native_stops==stops && calls==reads && !policy_writes);
    /* Only the proven physical rebuild clears the fault. Final replay applies
     * frozen policy before the caller can publish its restored leases. */
    new_driver();assert(!s_radio.fault_stage);
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(wifi_radio_restart_configs_pre_start_locked(&token,WIFI_MODE_STA)==ESP_OK);
    s_radio.started=s_radio.stop_required=native_running=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK && policy_writes==1);
    assert(native_twt_policy.post_wakeup_event && !native_twt_policy.twt_enable_keep_alive && !s_twt_policy.pending);
    assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);
    wipe();wifi_radio_operation_unlock();assert(!allocation && !locks && !critical);return 0;
}
'''
