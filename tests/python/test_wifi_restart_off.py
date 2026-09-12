"""Deferred off-source capture and final STOP/off/storage production suffix.

Uses actual capture/replay/commit and final off coordinator. Physical generation,
SDK calls, START lease delivery and STOP events are injected boundaries. The
shared actual resume ordering is covered in test_wifi_policy_replay; these
fixtures do not establish SDK normalization, RF, NVS or scheduler correctness.
"""
import unittest

from test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from test_wifi_config_controls import RADIO
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiRestartOff(unittest.TestCase):
    def test_off_capture_replay_final_stop_storage_and_every_sdk_failure(self):
        helpers = CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    code = config_code(profile, ap, mutation_boundary=True)
                    getter = 'static int esp_wifi_get_mode(wifi_mode_t *mode) {*mode=native_mode;return sdk_step(false);}'
                    self.assertEqual(code.count(getter), 1)
                    code = code.replace(getter, 'static bool off_readback_corrupt;\n' +
                        'static int esp_wifi_get_mode(wifi_mode_t *mode) {'
                        '*mode=off_readback_corrupt && native_mode==WIFI_MODE_NULL ? WIFI_MODE_STA : native_mode;'
                        'return sdk_step(false);}')
                    code += BOUNDARIES
                    code += extract(RADIO.read_text(), 'esp32_mquickjs_wifi_radio_resume_off_restart_lifecycle')
                    compile_run(self, code + helpers + MAIN)


BOUNDARIES = r'''
/* Native START/lease delivery boundary. Replay and configuration acceptance
 * below are the actual production helpers, with SDK calls fault-injected. */
static unsigned temporary_releases;
static int wifi_radio_resume_lifecycle_locked(esp32_mquickjs_wifi_radio_lifecycle_t *t,
    wifi_mode_t mode,bool start,esp32_mquickjs_wifi_radio_lease_t *application,
    esp32_mquickjs_wifi_radio_lease_t *station,esp32_mquickjs_wifi_radio_lease_t *ap,
    const void *controls,bool keep_lifecycle,void *enterprise_profile) {
    assert(locks==1 && !critical && start && application && !station && !ap && !controls && keep_lifecycle && !enterprise_profile);
    if(!wifi_radio_restart_configs_ready_locked(t,mode) || !s_config_restart.snapshot ||
        !s_config_restart.snapshot->restore_off || s_radio.started || s_radio.stop_required ||
        s_radio.fault_stage || s_radio.cleanup_stage)return ESP_ERR_INVALID_STATE;
    int error=wifi_radio_restart_configs_pre_start_locked(t,mode);
    if(error)return error;
    error=esp_wifi_start();s_radio.stop_required=true;s_radio.started=error==ESP_OK;
    if(error)return wifi_radio_record_fault("start",error);
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    error=wifi_radio_restart_configs_post_start_locked(t);
    if(!error)error=wifi_radio_restart_configs_commit_locked(t,mode);
    if(error)return error;
    assert(native_storage==WIFI_STORAGE_RAM && s_radio.storage==WIFI_STORAGE_RAM && allocation);
    application->identity=71;application->generation=s_radio.generation;application->acquired=true;
    return ESP_OK;
}
static void wifi_radio_release_locked(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(locks==1 && !critical);
    if(lease->acquired){assert(allocation && s_radio.lifecycle.identity==41);++temporary_releases;}
    memset(lease,0,sizeof(*lease));
}
static int wifi_radio_check_stopped_lifecycle_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,bool cleanup) {
    assert(!cleanup && locks==1 && !critical);
    return wifi_radio_restart_configs_owner(t) && !s_radio.started && !s_radio.stop_required &&
        s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_STOPPED ? ESP_OK : ESP_ERR_INVALID_STATE;
}
'''


MAIN = r'''
static void source(wifi_storage_t storage) {
    setup();temporary_releases=0;off_readback_corrupt=false;
    s_radio.started=s_radio.stop_required=native_running=false;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.effective_mode=native_mode=WIFI_MODE_NULL;s_radio.storage=native_storage=storage;
    wifi_radio_operation_lock();
}
static void dispose(void) {
    fail_at=0;cleanup_error=0;
    assert(wifi_radio_stop_locked()==ESP_OK);
    wipe();wifi_radio_operation_unlock();
}
int main(void) {
    const wifi_storage_t stores[]={WIFI_STORAGE_RAM,WIFI_STORAGE_FLASH};
    for(unsigned ai=0;ai<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT?2U:1U);++ai) {
        wifi_mode_t work=ai?WIFI_MODE_APSTA:WIFI_MODE_STA;
        for(unsigned si=0;si<2;++si) {
            unsigned capture_steps=0;
            for(unsigned failure=0;failure<=capture_steps;++failure) {
                source(stores[si]);fail_at=failure;
                int error=wifi_radio_restart_configs_capture_locked(&token,work);
                if(!failure) {
                    assert(!error);capture_steps=calls;
                    assert(s_config_restart.captured && s_config_restart.snapshot->restore_off);
                    assert(s_config_restart.snapshot->storage==stores[si]);
                    assert(!memcmp(s_config_restart.snapshot->saved[0].sta.password,"secret-sta",10));
                    assert(native_storage==WIFI_STORAGE_RAM && !nvs_writes);
                } else {
                    assert(error==77 && !s_config_restart.captured && !nvs_writes);
                    if(s_radio.configuration.mutation_attempted) {
                        assert(allocation && !frees && s_config_restart.snapshot->restore_off);
                        assert(s_config_restart.snapshot->capture_phase==RESTART_CAPTURE_FAILED);
                        unsigned before=calls;
                        assert(wifi_radio_restart_configs_capture_locked(&token,work)!=ESP_OK && calls==before);
                    } else assert(!allocation && !native_starts);
                }
                dispose();
            }
            unsigned suffix_steps=0;
            for(unsigned failure=0;failure<=suffix_steps;++failure) {
                source(stores[si]);
                assert(!wifi_radio_restart_configs_capture_locked(&token,work));
                assert(!wifi_radio_stop_locked());
                new_driver();assert(!wifi_radio_restart_configs_replay_locked(&token));
                unsigned before=calls;
                assert(wifi_radio_restart_configs_commit_storage_locked(&token)==ESP_ERR_INVALID_STATE);
                assert(calls==before && native_storage==WIFI_STORAGE_RAM);
                calls=writes=0;fail_at=failure;
                wifi_radio_operation_unlock();
                int error=esp32_mquickjs_wifi_radio_resume_off_restart_lifecycle(&token,work);
                assert(allocation && !frees && token.identity==41 && s_radio.lifecycle.identity==41);
                assert(!nvs_writes && s_config_restart.snapshot->restore_off);
                if(!failure) {
                    assert(!error);suffix_steps=calls;
                    assert(!s_radio.started && !s_radio.stop_required && !native_running);
                    assert(s_radio.effective_mode==WIFI_MODE_NULL && native_mode==WIFI_MODE_NULL);
                    assert(s_radio.storage_configured && s_radio.storage==stores[si] && native_storage==stores[si]);
                    assert(temporary_releases==1 && s_config_restart.snapshot->storage==stores[si]);
                    for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)assert(!s_radio.leases[i].identity);
                    /* Helper retirement is external; only its later finish may wipe the checkpoint. */
                    assert(!esp32_mquickjs_wifi_radio_finish_lifecycle(&token,false));
                    assert(!allocation && frees==1 && !token.identity);
                } else {
                    assert(error==77);
                    wifi_radio_operation_lock();dispose();
                }
            }
        }
    }
    source(WIFI_STORAGE_FLASH);
    assert(!wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA));
    assert(!wifi_radio_stop_locked());
    new_driver();assert(!wifi_radio_restart_configs_replay_locked(&token));
    unsigned before=calls;
    esp32_mquickjs_wifi_radio_lifecycle_t foreign=token;foreign.identity++;
    wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_resume_off_restart_lifecycle(&foreign,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(calls==before && allocation && token.identity==41);
    off_readback_corrupt=true;
    assert(esp32_mquickjs_wifi_radio_resume_off_restart_lifecycle(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE);
    assert(native_storage==WIFI_STORAGE_RAM && allocation && token.identity==41 && !nvs_writes);
    assert(!strcmp(s_radio.fault_stage,"restart-off-mode-readback"));
    off_readback_corrupt=false;wifi_radio_operation_lock();dispose();
    assert(!allocation && !locks && !critical && !helper_locks);
    return 0;
}
'''
