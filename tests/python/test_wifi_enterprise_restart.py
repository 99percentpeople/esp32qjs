"""Deferred Enterprise restart executor/AP/cleanup composition.

Actual runtime and AP coordinator bodies; injected Radio/SDK phase boundaries
assert retention and ordering. Installer, native admission and final publication
are exercised in the separate production EAP and policy fixtures. No RF proof.
"""
import unittest

from test_wifi_configuration_cleanup import BOUNDARIES as CLEANUP_BOUNDARIES, WIFI, AP
from test_wifi_enterprise_stop import EAP_BOUNDARIES
from test_wifi_restart_runtime import BOUNDARIES as RESTART_BOUNDARIES, MAIN as RESTART_MAIN
from test_wifi_config_controls import structure
from test_wifi_rx_target import INTERNAL, unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, extract


def enterprise_restart_code():
    code = '#include <stdbool.h>\n#include <stdlib.h>\nstatic bool ap_release_blocked;\n' + CLEANUP_BOUNDARIES
    code = code.replace('assert(cleanup);return exact(t)', '(void)cleanup;return exact(t)')
    code = code.replace('assert(!finish && native_token.identity && !running && !s_ap_netif);',
                        'assert(!finish && native_token.identity && !running);')
    code = code.replace('if(!sta_storage)return 0;',
                        'if(!sta_storage){s_wifi_state.runtime_cleanup_pending=false;return 0;}')
    code = code.replace('if(owner->acquired){', 'if(owner==&s_ap_lease && ap_release_blocked)return;\n    if(owner->acquired){')
    code += '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n'
    code += 'typedef struct esp32_mquickjs_wifi_eap_profile esp32_mquickjs_wifi_eap_profile_t;\n'
    code += unit(INTERNAL / 'esp32_mquickjs_wifi_eap_config.h')
    eap = EAP_BOUNDARIES.replace('static bool s_wifi_ap_stop_cleanup;', '')
    eap = eap.replace('action==ESP32_MQUICKJS_WIFI_EAP_CONFIG_DISABLE',
                      '(action==ESP32_MQUICKJS_WIFI_EAP_CONFIG_DISABLE || action==ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE)')
    code += eap
    code += structure((INTERNAL / 'esp32_mquickjs_wifi.h').read_text(), 'esp32_mquickjs_wifi_configuration_execution_t')
    for name in ('esp32_mquickjs_wifi_radio_stop_snapshot_t', 'esp32_mquickjs_wifi_radio_restart_selection_t'):
        code += structure((INTERNAL / 'esp32_mquickjs_wifi_radio.h').read_text(), name)
    code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
    code += RESTART_BOUNDARIES
    code += NATIVE_BOUNDARIES
    ap, wifi = AP.read_text(), WIFI.read_text()
    for name in ('wifi_ap_retire_netif', 'wifi_ap_retire_for_token',
                 'esp32_mquickjs_wifi_ap_begin_configuration', 'esp32_mquickjs_wifi_ap_begin_enterprise_restart',
                 'esp32_mquickjs_wifi_ap_begin_enterprise_stop', 'esp32_mquickjs_wifi_ap_begin_stopped_restart',
                 'esp32_mquickjs_wifi_ap_release_enterprise_stop',
                 'esp32_mquickjs_wifi_ap_retire_for_configuration', 'esp32_mquickjs_wifi_ap_retire_for_recovery'):
        code += extract(ap, name)
    for name in ('esp32_mquickjs_wifi_connection_reserved_locked', 'wifi_helpers_idle',
                 'esp32_mquickjs_wifi_retire_for_recovery', 'wifi_begin_configuration_cleanup',
                 'wifi_begin_enterprise_stop', 'wifi_finish_enterprise_clear', 'wifi_finish_configuration_cleanup',
                 'wifi_restart_restore_interfaces', 'wifi_begin_enterprise_restart', 'wifi_restart_enterprise_interfaces',
                 'wifi_restart_retry_interfaces', 'wifi_restart_interfaces_inner',
                 'esp32_mquickjs_wifi_restart_stopped_interfaces', 'esp32_mquickjs_wifi_eap_prepare_runtime_destroy'):
        code += extract(wifi, name)
    return code + RESTART_MAIN[:RESTART_MAIN.index('int main(void)')]


class WiFiEnterpriseRestart(unittest.TestCase):
    def test_source_clear_reinstall_failure_explicit_retry_and_runtime_retirement(self):
        compile_run(self, enterprise_restart_code() + MAIN)


NATIVE_BOUNDARIES = r'''
static int reinstall_error;
static unsigned reinstalls;
static uint64_t next_eap_binding=200;
static int esp32_mquickjs_wifi_radio_eap_begin_restart(uint64_t binding,
    const esp32_mquickjs_wifi_radio_lease_t *app,const esp32_mquickjs_wifi_radio_lease_t *sta,
    const esp32_mquickjs_wifi_radio_lease_t *ap,esp32_mquickjs_wifi_eap_profile_t *profile,bool allow,
    esp32_mquickjs_wifi_radio_lifecycle_t *token,wifi_mode_t *mode){
    assert(config_busy&&binding==eap_binding&&profile==(void *)1);
    if(!running || !sta_storage || s_wifi_state.status.connected || (ap->acquired&&!allow))return ESP_ERR_INVALID_STATE;
    int error=esp32_mquickjs_wifi_radio_begin_lifecycle(app,sta,ap,token);
    if(!error)*mode=ap->acquired?WIFI_MODE_APSTA:WIFI_MODE_STA;
    return error;
}
static int esp32_mquickjs_wifi_radio_eap_resume_restart(esp32_mquickjs_wifi_radio_lifecycle_t *t,
    wifi_mode_t mode,esp32_mquickjs_wifi_radio_lease_t *app,esp32_mquickjs_wifi_radio_lease_t *sta,
    esp32_mquickjs_wifi_radio_lease_t *ap,esp32_mquickjs_wifi_eap_profile_t *profile){
    assert(config_busy&&!config_closing&&profile==(void *)1&&!eap_binding&&checkpoint&&replayed&&exact(t));
    ++reinstalls;
    if(reinstall_error){
        assert(!app->acquired&&!sta->acquired&&(!ap||!ap->acquired));
        running=true;eap_binding=++next_eap_binding;return reinstall_error;
    }
    int error=esp32_mquickjs_wifi_radio_resume_lifecycle(t,mode,true,app,sta,ap,NULL);
    if(!error)eap_binding=++next_eap_binding;
    return error;
}
'''

MAIN = r'''
static void setup_enterprise(bool ap){
    setup_restart();memset(&s_wifi_eap_stop,0,sizeof(s_wifi_eap_stop));
    eap_binding=++next_eap_binding;config_busy=config_closing=false;
    clear_error=reinstall_error=0;clear_calls=config_begins=config_finishes=reinstalls=0;
    retry_allowed=true;ap_release_blocked=false;
    if(ap){s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};s_ap_netif=(void *)1;}
}
#define RESTART(allow) esp32_mquickjs_wifi_restart_stopped_interfaces(allow,&result)
int main(void){
    esp32_mquickjs_wifi_configuration_execution_t result;
    setup_enterprise(true);
    uint64_t original=eap_binding;
    assert(RESTART(false)==ESP_ERR_INVALID_STATE&&!result.admitted&&!s_wifi_lifecycle.identity);
    assert(!clear_calls&&!owner_releases&&!config_busy&&eap_binding==original&&!temporary);
    s_wifi_state.status.connected=true;assert(RESTART(true)==ESP_ERR_INVALID_STATE&&!clear_calls);
    s_wifi_state.status.connected=false;config_closing=true;
    assert(RESTART(true)==ESP_ERR_INVALID_STATE&&!clear_calls);config_closing=false;
    /* A completed SDK clear does not repeat when AP release is still blocked. */
    ap_release_blocked=true;
    assert(RESTART(true)==ESP_ERR_INVALID_STATE&&config_busy&&s_wifi_lifecycle.identity);
    assert(clear_calls==1&&!eap_binding&&!s_wifi_eap_stop.binding&&s_wifi_eap_stop.release_ap&&!phase_calls[1]);
    assert(!strcmp(s_wifi_state.cleanup_stage,"configuration-enterprise-ap-release"));
    ap_release_blocked=false;
    assert(!RESTART(true)&&clear_calls==1&&reinstalls==1&&!config_busy&&!s_wifi_eap_stop.profile);
    assert(running&&s_wifi_application.acquired&&s_wifi_state.radio_lease.acquired&&s_ap_lease.acquired);
    for(int ap=0;ap<=1;++ap){
        setup_enterprise(ap);clear_error=91;
        assert(RESTART(ap)==91&&config_busy&&eap_binding&&s_wifi_eap_stop.profile);
        assert(!phase_calls[1]&&!owner_releases&&!s_wifi_eap_stop.checkpoint_attempted);
        clear_error=0;reinstall_error=92;
        assert(RESTART(ap)==92&&config_busy&&s_wifi_lifecycle.identity&&s_wifi_eap_stop.binding==eap_binding);
        assert(checkpoint&&reconstructed&&replayed&&!s_wifi_eap_stop.release_ap&&!s_ap_coordinator.identity);
        assert(!s_wifi_application.acquired&&!s_wifi_state.radio_lease.acquired&&!s_ap_lease.acquired);
        unsigned clears_before=clear_calls,stops_before=stop_calls;
        clear_error=93;
        assert(RESTART(ap)==93&&clear_calls==clears_before+1&&stop_calls==stops_before);
        assert(!strcmp(s_wifi_state.cleanup_stage,"configuration-enterprise-clear")&&config_busy);
        clear_error=reinstall_error=0;
        assert(!RESTART(ap)&&reinstalls==2&&!config_busy&&!s_wifi_lifecycle.identity);
        assert(phase_calls[1]==1&&phase_calls[2]==2&&eap_binding&&!temporary);
        /* Config close bars reinstall and hands this same operation to cleanup. */
        setup_enterprise(ap);reinstall_error=94;
        assert(RESTART(ap)==94&&config_busy&&s_wifi_eap_stop.binding);
        clear_error=95;
        assert(!esp32_mquickjs_wifi_eap_prepare_runtime_destroy()&&config_closing&&config_busy);
        unsigned installs_before=reinstalls;
        assert(RESTART(ap)==ESP_ERR_INVALID_STATE&&reinstalls==installs_before);
        clear_error=0;
        assert(esp32_mquickjs_wifi_eap_prepare_runtime_destroy()&&!config_busy&&!eap_binding);
        assert(!s_wifi_lifecycle.identity&&!s_wifi_configuration_cleanup&&!s_wifi_eap_stop.profile);
    }
    /* Incomplete source capture can be retired, but cannot be recaptured. */
    setup_enterprise(false);phase_failure=1;
    assert(RESTART(false)==-101&&s_wifi_eap_stop.checkpoint_attempted);
    checkpoint=false; /* Native capture boundary reports an incomplete snapshot. */
    phase_failure=0;assert(RESTART(false)==ESP_ERR_INVALID_STATE&&phase_calls[1]==1);
    assert(!wifi_finish_configuration_cleanup()&&!config_busy&&!eap_binding&&!s_wifi_lifecycle.identity);
    return 0;
}
'''
