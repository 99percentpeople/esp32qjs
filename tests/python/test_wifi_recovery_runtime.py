"""Deferred production recovery stepper, AP handoff and shared restoration.

SDK/Radio physical phases and owner retirement are injected boundaries, with
actual Radio production coverage in test_wifi_action_recovery. No fixture
import, C compilation or execution until the Wi-Fi implementation phase ends.
"""
import re
import unittest

from test_wifi_restart_runtime import runtime_code, MAIN as RESTART_MAIN
from test_wifi_configuration_cleanup import WIFI, AP
from test_wifi_config_controls import structure, HEADER
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def recovery_request_code(ftm=True, twt=False):
    header = HEADER.read_text()
    return (('#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n#define CONFIG_IDF_TARGET_ESP32C5 1\n' if twt else '') +
            f'#define CONFIG_ESP_WIFI_FTM_ENABLE {int(ftm)}\n#define CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT {int(ftm)}\n' +
            re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_recovery_kind_t;', header).group(0) + '\n' +
            structure(header, 'esp32_mquickjs_wifi_recovery_request_t') + '\n')


def recovery_runtime_code(ap):
    source = WIFI.read_text()
    code = runtime_code(ap)
    # Mirror wifi_init_helper's existing rejection boundary, including the
    # diagnostic stage left by the recovery stepper after native retirement.
    code = code.replace('!s_wifi_state.runtime_cleanup_pending);',
                        '!s_wifi_state.runtime_cleanup_pending && !s_wifi_state.cleanup_stage && !s_wifi_state.cleanup_error);')
    code += recovery_request_code()
    code += structure((WIFI.parents[3] / 'internal/esp32_mquickjs_wifi.h').read_text(),
                      'esp32_mquickjs_wifi_recovery_t')
    code += re.search(r'enum \{\s*WIFI_RECOVERY_NEW,.*?\n\};', source, re.S).group(0)
    code += BOUNDARIES
    ap_source = AP.read_text()
    if not ap:
        ap_source = ap_source[ap_source.rindex('esp_err_t esp32_mquickjs_wifi_ap_begin_recovery('):]
    code += extract(ap_source, 'esp32_mquickjs_wifi_ap_begin_recovery')
    code += ''.join(extract(source, name) for name in (
        'esp32_mquickjs_wifi_recovery_dispose',
        'esp32_mquickjs_wifi_recovery_begin', 'esp32_mquickjs_wifi_recovery_step'))
    code += RESTART_MAIN[:RESTART_MAIN.index('int main(void)')]
    return code


class WiFiRecoveryRuntime(unittest.TestCase):
    def test_exact_admission_yield_to_native_owner_and_shared_restoration(self):
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, recovery_runtime_code(ap) + MAIN)

    def test_disposal_transfers_cleanup_and_reconstruction_failure_uses_normal_cleanup(self):
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, recovery_runtime_code(ap) + FAILURE_MAIN)


BOUNDARIES = r'''
static wifi_mode_t recovery_mode;
static unsigned recovery_handoffs;
static esp32_mquickjs_wifi_recovery_request_t original={9,51};
static int esp32_mquickjs_wifi_radio_begin_recovery(
    const esp32_mquickjs_wifi_radio_lease_t *app,const esp32_mquickjs_wifi_radio_lease_t *sta,
    const esp32_mquickjs_wifi_radio_lease_t *ap,const esp32_mquickjs_wifi_recovery_request_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token,wifi_mode_t *mode) {
    assert(app==&s_wifi_application && sta==&s_wifi_state.radio_lease);
    assert(ap==(CONFIG_ESP_WIFI_SOFTAP_SUPPORT?&s_ap_lease:NULL));
    assert(!token->identity && !token->generation);++begin_calls;*mode=WIFI_MODE_NULL;
    if(foreign_owner || operation->identity!=original.identity || operation->generation!=original.generation || operation->kind!=original.kind)
        return ESP_ERR_INVALID_STATE;
    native_token=(esp32_mquickjs_wifi_radio_lifecycle_t){9,41};*token=native_token;
    *mode=recovery_mode;recovery_admitted=true;return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_checkpoint_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token,wifi_mode_t mode) {
    assert(recovery_admitted && running);target_mode=mode;
    int err=phase(1,token);if(!err)checkpoint=true;return err;
}
static int esp32_mquickjs_wifi_radio_finish_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(exact(token) && recovery_admitted && !recovery_owner_pending && !running);
    assert(!sta_storage && !s_ap_netif);++recovery_handoffs;recovery_admitted=false;return ESP_OK;
}
'''

MAIN = r'''
static void check_kind(esp32_mquickjs_wifi_recovery_kind_t kind) {
    original.kind=kind;
    esp32_mquickjs_wifi_recovery_t state={0};bool complete;
    setup_restart();recovery_mode=WIFI_MODE_STA;
    esp32_mquickjs_wifi_recovery_request_t invalid=original;invalid.kind=99;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&invalid,false)==ESP_ERR_INVALID_ARG);
    assert(!begin_calls && !temporary && !owner_releases);
    s_wifi_state.status.connected=true;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,false)==ESP_ERR_INVALID_STATE);
    assert(!begin_calls && !temporary && s_wifi_application.acquired);
    s_wifi_state.status.connected=false;s_wifi_state.scan_future_registered=true;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,true)==ESP_ERR_INVALID_STATE);
    s_wifi_state.scan_future_registered=false;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    alloc_fail=true;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,false)==ESP_ERR_NO_MEM && !begin_calls);
    alloc_fail=false;
#endif
    esp32_mquickjs_wifi_recovery_request_t wrong_kind=original;
    wrong_kind.kind=kind==ESP32_MQUICKJS_WIFI_RECOVERY_ACTION?ESP32_MQUICKJS_WIFI_RECOVERY_FTM:ESP32_MQUICKJS_WIFI_RECOVERY_ACTION;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&wrong_kind,false)==ESP_ERR_INVALID_STATE);
    assert(!state.execution.admitted && !temporary && !owner_releases);
    esp32_mquickjs_wifi_recovery_request_t stale=original;++stale.identity;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&stale,false)==ESP_ERR_INVALID_STATE);
    assert(!state.execution.admitted && !temporary && !owner_releases);
    for(int mode=1;mode<=(CONFIG_ESP_WIFI_SOFTAP_SUPPORT?3:1);++mode) {
        setup_restart();recovery_mode=mode;recovery_owner_pending=true;
        recovery_shutdown_calls=recovery_handoffs=0;
        if(mode&WIFI_MODE_AP){s_ap_netif=(void *)1;s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};}
        if(mode==WIFI_MODE_AP && original.kind==ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX) {
            s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){0};
            s_ap_raw_owner.generation=9;s_ap_raw_owner.identity=73;
        }
        assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,false)==ESP_OK);
        assert(state.execution.admitted && s_wifi_lifecycle.identity==41 && s_wifi_configuration_cleanup);
        assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,false)==ESP_ERR_INVALID_ARG);
        for(unsigned i=0;i<4;++i) {
            assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && !complete);
            if(i==0)assert(running && checkpoint && !stop_calls);
        }
        assert(!running && !sta_storage && !s_ap_netif && !s_ap_raw_owner.identity);
        assert(phase_calls[1]==1 && stop_calls==1 && !phase_calls[2]);
        assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && !complete);
        assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && !complete);
        assert(recovery_shutdown_calls==2 && !recovery_handoffs && !phase_calls[2]);
        assert(!strcmp(state.execution.stage,"recovery-native-drain"));
        /* Only this injected native owner boundary consumes physical proof. */
        recovery_owner_pending=false;
        assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && !complete);
        assert(recovery_handoffs==1 && !recovery_admitted && s_wifi_lifecycle.identity==41);
        assert(s_wifi_state.runtime_cleanup_pending && s_wifi_state.cleanup_stage);
        assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && complete);
        assert(state.execution.resume_attempted && !s_wifi_lifecycle.identity && !s_wifi_configuration_cleanup);
        assert(phase_calls[1]==1 && phase_calls[2]==1 && phase_calls[5]==1 && phase_calls[9]==1);
        assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && complete && phase_calls[2]==1);
        esp32_mquickjs_wifi_recovery_dispose(&state);
        assert(!state.phase && !state.lifecycle.identity && !temporary);
    }
}
int main(void) {
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_ACTION);
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX);
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_FTM);
    return 0;
}
'''

FAILURE_MAIN = r'''
static void check_kind(esp32_mquickjs_wifi_recovery_kind_t kind) {
    original.kind=kind;
    for(unsigned point=0;point<6;++point) {
        setup_restart();recovery_mode=WIFI_MODE_STA;recovery_admitted=false;recovery_owner_pending=true;
        esp32_mquickjs_wifi_recovery_t state={0};bool complete;
        assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,false)==ESP_OK);
        for(unsigned step=0;step<point;++step)
            assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && !complete);
        esp32_mquickjs_wifi_recovery_dispose(&state);
        assert(!temporary && s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==41);
        assert(wifi_finish_configuration_cleanup()==ESP_ERR_TIMEOUT);
        recovery_owner_pending=false;
        assert(wifi_finish_configuration_cleanup()==ESP_OK && !s_wifi_lifecycle.identity);
    }
    setup_restart();recovery_mode=WIFI_MODE_STA;recovery_admitted=false;recovery_owner_pending=false;
    esp32_mquickjs_wifi_recovery_t state={0};bool complete;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,false)==ESP_OK);
    for(unsigned step=0;step<5;++step)
        assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && !complete);
    phase_failure=2;
    assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==-102 && !complete);
    assert(!recovery_admitted && !strcmp(state.execution.stage,"restart-rebuild"));
    unsigned old_rebuild=phase_calls[2];
    assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_ERR_INVALID_STATE);
    assert(phase_calls[2]==old_rebuild);
    phase_failure=0;
    assert(wifi_finish_configuration_cleanup()==ESP_OK && !s_wifi_lifecycle.identity);
    esp32_mquickjs_wifi_recovery_dispose(&state);assert(!temporary);

    setup_restart();recovery_mode=WIFI_MODE_STA;recovery_admitted=false;recovery_owner_pending=false;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,false)==ESP_OK);
    assert(wifi_finish_configuration_cleanup()==ESP_OK);
    /* A different lifecycle now owns the coordinator: stale step cannot touch it. */
    native_token=s_wifi_lifecycle=(esp32_mquickjs_wifi_radio_lifecycle_t){10,42};
    s_wifi_configuration_cleanup=true;
    unsigned before=stop_calls;
    assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_ERR_INVALID_STATE);
    assert(s_wifi_lifecycle.identity==42 && stop_calls==before);
    esp32_mquickjs_wifi_recovery_dispose(&state);assert(!temporary);
}
int main(void) {
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_ACTION);
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX);
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_FTM);
    return 0;
}
'''
