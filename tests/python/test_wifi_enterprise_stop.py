"""Deferred production stop/AP handoff and cleanup suffix, with SDK/config boundaries.

Exact Radio binding/lifecycle proof is in test_wifi_eap_radio. Config/profile
retention has its own production fixture; this fixture exercises the real helper
coordinator including retries after AP coordinator storage has retired.
"""
import unittest
from test_wifi_configuration_cleanup import BOUNDARIES, WIFI, AP
from test_wifi_rx_target import INTERNAL, unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiEnterpriseStop(unittest.TestCase):
    def test_admission_clear_failure_and_completed_suffix_not_repeated(self):
        wifi, ap = WIFI.read_text(), AP.read_text()
        code = BOUNDARIES
        code += '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1\n#define WIFI_MODE_STA 1\n#define WIFI_MODE_APSTA 3\n'
        code += 'typedef struct esp32_mquickjs_wifi_eap_profile esp32_mquickjs_wifi_eap_profile_t;\n'
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_eap_config.h')
        code += EAP_BOUNDARIES
        for name in ['wifi_ap_retire_netif','wifi_ap_retire_for_token',
                     'esp32_mquickjs_wifi_ap_begin_enterprise_stop',
                     'esp32_mquickjs_wifi_ap_begin_configuration',
                     'esp32_mquickjs_wifi_ap_release_enterprise_stop',
                     'esp32_mquickjs_wifi_ap_retire_for_configuration',
                     'esp32_mquickjs_wifi_ap_retire_for_recovery']:
            code += extract(ap, name)
        for name in ['esp32_mquickjs_wifi_connection_reserved_locked','wifi_helpers_idle','esp32_mquickjs_wifi_retire_for_recovery',
                     'wifi_begin_configuration_cleanup','wifi_begin_enterprise_stop',
                     'wifi_finish_enterprise_clear','wifi_finish_configuration_cleanup','wifi_stop_idle',
                     'esp32_mquickjs_wifi_eap_prepare_runtime_destroy']:
            code += extract(wifi, name)
        compile_run(self, code + MAIN)


EAP_BOUNDARIES = r'''
static bool s_wifi_ap_stop_cleanup;
static int wifi_adopt_ap_stop_cleanup(void){assert(0);return ESP_ERR_INVALID_STATE;}
static int wifi_cleanup_failed_init(void){assert(0);return ESP_ERR_INVALID_STATE;}
static struct {esp32_mquickjs_wifi_eap_config_token_t control;uint64_t binding;
    esp32_mquickjs_wifi_eap_profile_t *profile;bool release_ap,checkpoint_attempted;} s_wifi_eap_stop;
typedef struct {int unused;} esp32_mquickjs_wifi_eap_install_result_t;
static bool config_busy,config_closing;
static unsigned config_begins,config_finishes,clear_calls;
static uint64_t eap_binding;
static int clear_error;
static uint64_t esp32_mquickjs_wifi_radio_eap_identity(void){return eap_binding;}
static const esp32_mquickjs_wifi_radio_lease_t *esp32_mquickjs_wifi_ap_control_lease(void){return s_ap_lease.acquired?&s_ap_lease:NULL;}
void esp32_mquickjs_wifi_eap_config_status(esp32_mquickjs_wifi_eap_config_status_t *s){
    *s=(esp32_mquickjs_wifi_eap_config_status_t){.revision=9,.busy=config_busy,.closing=config_closing};
}
esp_err_t esp32_mquickjs_wifi_eap_config_begin(uint64_t revision,esp32_mquickjs_wifi_eap_config_action_t action,
    esp32_mquickjs_wifi_eap_config_token_t *t,esp32_mquickjs_wifi_eap_profile_t **p){
    assert(revision==9 && action==ESP32_MQUICKJS_WIFI_EAP_CONFIG_DISABLE && !t->identity);
    if(config_busy || config_closing)return ESP_ERR_INVALID_STATE;
    *t=(esp32_mquickjs_wifi_eap_config_token_t){88,9,action};*p=(void *)1;config_busy=true;config_begins++;return ESP_OK;
}
esp_err_t esp32_mquickjs_wifi_eap_config_finish(esp32_mquickjs_wifi_eap_config_token_t *t,bool discard){
    assert(config_busy && t->identity==88 && !discard);config_finishes++;config_busy=false;memset(t,0,sizeof(*t));return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_eap_begin_stop(uint64_t binding,
    const esp32_mquickjs_wifi_radio_lease_t *app,const esp32_mquickjs_wifi_radio_lease_t *sta,
    const esp32_mquickjs_wifi_radio_lease_t *ap,esp32_mquickjs_wifi_radio_lifecycle_t *token){
    assert(binding==eap_binding && binding && config_busy);
    return esp32_mquickjs_wifi_radio_begin_lifecycle(app,sta,ap,token);
}
void esp32_mquickjs_wifi_eap_config_begin_close(void){config_closing=true;}
bool esp32_mquickjs_wifi_eap_config_finish_close(void){return config_closing && !config_busy && !eap_binding;}
static int esp32_mquickjs_wifi_start_disconnect(bool *pending){(void)pending;assert(0);return ESP_ERR_INVALID_STATE;}
static bool esp32_mquickjs_wifi_drain_scan(void){assert(0);return false;}
static int esp32_mquickjs_wifi_radio_eap_clear(uint64_t id,esp32_mquickjs_wifi_eap_install_result_t *r){
    (void)id;(void)r;assert(0);return ESP_ERR_INVALID_STATE;
}
static int esp32_mquickjs_wifi_radio_eap_clear_lifecycle(uint64_t binding,
    const esp32_mquickjs_wifi_radio_lifecycle_t *token,esp32_mquickjs_wifi_eap_install_result_t *result){
    (void)result;assert(exact(token) && config_busy);clear_calls++;
    if(!eap_binding)return ESP_OK;
    assert(binding==eap_binding);if(clear_error)return clear_error;eap_binding=0;return ESP_OK;
}
'''
MAIN = r'''
int main(void){
    for(int stage=0;stage<=4;stage++){
        s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
        s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
        s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
        s_ap_netif=(void *)1;sta_storage=running=true;expect_stop_only=true;eap_binding=123;
        owner_releases=clear_calls=config_begins=config_finishes=0;
        s_wifi_state.status.connected=true;
        assert(wifi_begin_enterprise_stop(eap_binding)==ESP_ERR_INVALID_STATE && !config_begins);
        s_wifi_state.status.connected=false;config_busy=true;
        assert(wifi_begin_enterprise_stop(eap_binding)==ESP_ERR_INVALID_STATE && !config_begins);
        config_busy=false;foreign_owner=true;
        assert(wifi_begin_enterprise_stop(eap_binding)==ESP_ERR_INVALID_STATE);
        assert(!config_busy && config_begins==1 && config_finishes==1 && !owner_releases);
        foreign_owner=false;
        assert(wifi_begin_enterprise_stop(eap_binding)==ESP_OK);
        assert(config_busy && s_wifi_configuration_stop_only && s_ap_coordinator.identity && s_ap_lease.acquired && !owner_releases);
        esp32_mquickjs_wifi_radio_lifecycle_t stale=s_wifi_lifecycle;++stale.identity;
        assert(esp32_mquickjs_wifi_ap_release_enterprise_stop(&stale)==ESP_ERR_INVALID_STATE && s_ap_lease.acquired);
        clear_error=82;
        assert(wifi_finish_configuration_cleanup()==82 && config_busy && !owner_releases);
        assert(eap_binding && s_wifi_eap_stop.binding && s_wifi_lifecycle.identity);
        assert(!strcmp(s_wifi_state.cleanup_stage,"configuration-enterprise-clear"));
        clear_error=0;fail_at=stage;
        int result=wifi_finish_configuration_cleanup();
        assert(result==(stage?-stage:0));
        assert(!eap_binding && !s_wifi_eap_stop.binding && owner_releases==3 && clear_calls==2);
        if(stage){
            assert(config_busy && s_wifi_eap_stop.control.identity && s_wifi_lifecycle.identity);
            if(stage>=3)assert(!s_ap_coordinator.identity); /* Completed AP suffix must not be repeated. */
            fail_at=0;assert(wifi_finish_configuration_cleanup()==ESP_OK && clear_calls==2);
        }
        assert(!config_busy && !s_wifi_eap_stop.control.identity && config_finishes==2);
        assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity && !s_ap_coordinator.identity);
    }
    /* Exercise the actual public stop helper, including joining its retained
     * transaction rather than rejecting its own busy config token on retry. */
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    s_ap_netif=(void *)1;sta_storage=running=true;eap_binding=124;
    config_busy=true;assert(wifi_stop_idle()==ESP_ERR_INVALID_STATE);config_busy=false;
    s_wifi_state.status.connected=true;assert(wifi_stop_idle()==ESP_ERR_INVALID_STATE);
    s_wifi_state.status.connected=false;clear_error=83;
    assert(wifi_stop_idle()==83 && config_busy && s_wifi_lifecycle.identity);
    clear_error=0;assert(wifi_stop_idle()==ESP_OK && !config_busy && !eap_binding);
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    s_ap_netif=(void *)1;sta_storage=running=true;eap_binding=125;clear_error=84;
    assert(wifi_stop_idle()==84 && config_busy);
    assert(!esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && config_busy && config_closing);
    clear_error=0;
    assert(esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && !config_busy && !eap_binding);
    assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity);
    return 0;
}
'''
