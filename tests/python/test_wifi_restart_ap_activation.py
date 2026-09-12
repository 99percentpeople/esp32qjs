"""Deferred production AP-only CSA then STA activation; SDK/scheduler boundaries.

The production event barrier is exercised separately by band_cycle_events.
Native CSA/association behavior still needs RF verification on each target.
"""
import re
import unittest
from wireless_vm_fixture import extract
from test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run

CLOCKS = r'''
#include <stdbool.h>
#include <stdint.h>
#define ESP_ERR_TIMEOUT 78
#define WIFI_RADIO_START_EVENT_TIMEOUT_MS 8
#define pdMS_TO_TICKS(v) (v)
typedef unsigned TickType_t;
typedef struct {int unused;} esp32_mquickjs_runtime_t;
typedef struct {int unused;} esp32_mquickjs_native_wait_t;
static unsigned ticks,waits,delays,wait_limit=8,station_activations;
static bool csa_pending,cancel_wait,never_settle,station_channel_drift;
static uint8_t csa_primary;
static int csa_secondary;
'''

BOUNDARIES = r'''
static TickType_t xTaskGetTickCount(void) {return ticks;}
static TickType_t esp32_mquickjs_wifi_wait_remaining(TickType_t fallback){return wait_limit<fallback?wait_limit:fallback;}
static void vTaskDelay(TickType_t t) {
    assert(!critical && waits==1 && native_mode==WIFI_MODE_AP);ticks+=t;delays++;
    if(csa_pending && !never_settle && delays==2) {
        native_channel=native_home=csa_primary;native_secondary=native_home_secondary=csa_secondary;
        native_band=csa_primary>14?WIFI_BAND_5G:WIFI_BAND_2G;csa_pending=false;
    }
}
static bool esp32_mquickjs_cooperate(esp32_mquickjs_runtime_t *runtime){(void)runtime;assert(waits==1);return !cancel_wait;}
static esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void){return NULL;}
static void esp32_mquickjs_native_wait_begin(esp32_mquickjs_runtime_t *r,esp32_mquickjs_native_wait_t *w){(void)r;(void)w;assert(!waits);waits=1;}
static void esp32_mquickjs_native_wait_end(esp32_mquickjs_runtime_t *r,esp32_mquickjs_native_wait_t *w){(void)r;(void)w;assert(waits==1);waits=0;}
static int wifi_radio_ensure_started_locked(esp32_mquickjs_wifi_radio_lease_t *lease){(void)lease;assert(!"AP activation must use its production path");return ESP_ERR_INVALID_STATE;}
'''

MAIN = r'''
static void prepare(wifi_mode_t mode,bool five) {
    setup();wifi_radio_operation_lock();ticks=waits=delays=station_activations=0;wait_limit=8;
    csa_pending=cancel_wait=never_settle=station_channel_drift=false;
    native_mode=s_radio.effective_mode=mode;native_config[1].ap.channel=1;
    native_channel=native_home=five?36:6;native_band=five?WIFI_BAND_5G:WIFI_BAND_2G;
    assert(wifi_radio_restart_configs_capture_locked(&token,mode)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(wifi_radio_restart_configs_pre_start_locked(&token,mode)==ESP_OK);
}
static esp32_mquickjs_wifi_radio_lease_t stage_owner(void) {
    esp32_mquickjs_wifi_radio_lease_t lease={.generation=s_radio.generation,.identity=88,.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,.acquired=true};
    s_radio.leases[0]=(wifi_radio_live_lease_t){lease.identity,lease.client};return lease;
}
int main(void) {
    for(unsigned both=0;both<2;++both)for(unsigned five=0;five<(CONFIG_SOC_WIFI_SUPPORT_5G?2U:1U);++five) {
        wifi_mode_t mode=both?WIFI_MODE_APSTA:WIFI_MODE_AP;
        unsigned total=0;
        for(unsigned failure=0;failure<=total;++failure) {
            prepare(mode,five);
            wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
            esp32_mquickjs_wifi_radio_lease_t lease=stage_owner();
            calls=writes=0;fail_at=failure;
            int err=wifi_radio_restart_configs_start_locked(&token,mode,&lease);
            if(!failure) {
                assert(err==ESP_OK);total=calls;
                assert(delays==2 && channel_writes==1 && station_activations==both);
                assert(native_channel==frozen.primary && native_home==frozen.primary && native_mode==mode);
                assert(native_config[1].ap.channel==1 && native_storage==WIFI_STORAGE_RAM && !nvs_writes);
                assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
                assert(wifi_radio_restart_configs_commit_locked(&token,mode)==ESP_OK && native_storage==WIFI_STORAGE_FLASH);
            } else {
                assert(err==77 && allocation && !frees && s_radio.fault_stage);
                unsigned before=calls,start_count=native_starts,channels=channel_writes;
                assert(wifi_radio_restart_configs_start_locked(&token,mode,&lease)==ESP_ERR_INVALID_STATE);
                assert(calls==before && native_starts==start_count && channel_writes==channels);
            }
            assert(!waits && !critical && token.identity==41 && !memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
            memset(s_radio.leases,0,sizeof(s_radio.leases));wipe();wifi_radio_operation_unlock();
        }
    }
    for(unsigned scenario=0;scenario<5;++scenario) {
        prepare(WIFI_MODE_APSTA,false);
        esp32_mquickjs_wifi_radio_lease_t lease=stage_owner();
        if(scenario==0)never_settle=true;
        if(scenario==1)cancel_wait=true;
        if(scenario==2)wait_limit=0;
        if(scenario==3)station_channel_drift=true;
        esp32_mquickjs_wifi_radio_lifecycle_t request=token;
        if(scenario==4)request.identity++;
        unsigned before=calls;
        int err=wifi_radio_restart_configs_start_locked(&request,WIFI_MODE_APSTA,&lease);
        assert(err==(scenario==1 || scenario==4?ESP_ERR_INVALID_STATE:scenario==3?ESP_ERR_INVALID_RESPONSE:ESP_ERR_TIMEOUT));
        assert(!waits && allocation && !frees && native_storage==WIFI_STORAGE_RAM);
        if(scenario==4)assert(calls==before && !s_radio.started);
        if(scenario<3)assert(!station_activations && channel_writes==1);
        if(scenario==3)assert(station_activations==1);
        memset(s_radio.leases,0,sizeof(s_radio.leases));wipe();wifi_radio_operation_unlock();
    }
    return 0;
}
'''


def activation_code(profile):
    code = config_code(profile, True)
    # SDK adapters preserve their faults while modeling asynchronous channel
    # completion and the AP-only -> APSTA mode transition, without AP restart.
    code = re.sub(r'static int wifi_radio_begin_events\([^)]*\) \{[\s\S]*?\n\}', r'''static int wifi_radio_begin_events(int phase,wifi_mode_t mode) {
        assert((phase==RADIO_EVENTS_START && (mode==WIFI_MODE_STA || mode==WIFI_MODE_AP) && !s_radio.started) ||
            (phase==RADIO_EVENTS_RESTART && mode==WIFI_MODE_STA && s_radio.started) ||
            (phase==RADIO_EVENTS_STA_START && mode==WIFI_MODE_STA && s_radio.started && native_mode==WIFI_MODE_AP));
        return sdk_step(false);
    }'''.replace('native_mode==WIFI_MODE_AP', 's_radio.effective_mode==WIFI_MODE_AP'), code, count=1)
    code = code.replace('for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)assert(!s_radio.leases[i].identity);\n    native_running=true;',
                        'if(native_mode==WIFI_MODE_AP){native_channel=native_home=native_config[1].ap.channel;native_band=WIFI_BAND_2G;}\n    native_running=true;')
    code = code.replace('static int esp_wifi_set_mode(wifi_mode_t mode) {assert(!s_radio.started);native_mode=mode;return sdk_step(true);}', r'''static int esp_wifi_set_mode(wifi_mode_t mode) {
        if(s_radio.started){assert(native_mode==WIFI_MODE_AP && mode==WIFI_MODE_APSTA && !csa_pending);
            station_activations++;if(station_channel_drift)native_channel=native_home=11;}
        native_mode=mode;return sdk_step(true);
    }''')
    code = re.sub(r'static int esp_wifi_set_channel\([^)]*\) \{[\s\S]*?\n\}', r'''static int esp_wifi_set_channel(uint8_t primary,wifi_second_chan_t secondary) {
        assert(s_radio.started && native_mode==WIFI_MODE_AP && native_storage==WIFI_STORAGE_RAM);
        ++channel_writes;csa_primary=primary;csa_secondary=secondary;csa_pending=true;return sdk_step(true);
    }''', code, count=1)
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = CLOCKS + code + BOUNDARIES
    for name in ('wifi_radio_restart_ap_channel_matches', 'wifi_radio_restart_ap_channel_wait_inner',
                 'wifi_radio_restart_ap_channel_wait', 'wifi_radio_restart_configs_start_locked'):
        code += extract(radio, name)
    return code


class WiFiRestartApActivation(unittest.TestCase):
    def test_ap_csa_then_station_start_faults_deadline_and_stale_token(self):
        helpers = CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, activation_code(profile) + helpers + MAIN)
