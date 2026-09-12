"""Deferred production EAP Radio pins and runtime disconnect/retirement gates.

The installer is an injected boundary here; its own fixture uses production
profile/transaction code. Radio registry, admission, release, EAP commands and
runtime prepare bodies are production source, not a parallel ownership model.
"""
import re
import unittest
from test_wifi_vendor_ie import vendor_code, COMPONENT
from test_wifi_config_controls import sdk_types
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, extract


class WiFiEAPRadio(unittest.TestCase):
    def code(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        code = vendor_code('esp32c5/representative', enterprise=True)
        before = sdk_types('esp32c5/representative')
        extra = sdk_types('esp32c5/representative', ('wifi_ap_record_t',))
        assert extra.startswith(before)
        code += extra[len(before):]
        for name in ('esp32_mquickjs_wifi_enterprise_profile.h', 'esp32_mquickjs_wifi_eap_sdk.h', 'esp32_mquickjs_wifi_eap_install.h', 'esp32_mquickjs_wifi_eap_config.h'):
            code += unit(COMPONENT / 'internal' / name)
        code += BOUNDARIES
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        for name in ('wifi_radio_connection_owner_locked','wifi_radio_eap_exact_locked',
                     'wifi_radio_eap_disconnected_locked','esp32_mquickjs_wifi_radio_eap_install',
                     'esp32_mquickjs_wifi_radio_eap_begin_stop', 'wifi_radio_eap_clear_locked',
                     'esp32_mquickjs_wifi_radio_eap_clear','esp32_mquickjs_wifi_radio_eap_clear_lifecycle',
                     'esp32_mquickjs_wifi_radio_eap_status',
                     'esp32_mquickjs_wifi_radio_eap_identity'):
            code += extract(radio, name)
        code += extract(wifi, 'esp32_mquickjs_wifi_eap_prepare_runtime_destroy')
        return code

    def test_pins_failed_install_exact_clear_and_runtime_retry(self):
        compile_run(self, self.code() + MAIN)

    def test_restart_exact_source_admission_and_cross_generation_failed_install_clear(self):
        code = self.code()
        code = code.replace('int lock;uint32_t generation,',
            'struct {const char *stage;int error;bool mutation_attempted;} configuration;\n'
            'unsigned event_phase,event_live;int lock;uint32_t generation,')
        code = code.replace('owner;bool uncertain;} s_interval;', 'owner;bool uncertain,restore_pending;} s_interval;')
        code = code.replace('unsigned identity,generation;} s_tx_rate_lease;',
                            'unsigned identity,generation;bool restore_pending;} s_tx_rate_lease;')
        code += RESTART_BOUNDARIES
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        for name in ('esp32_mquickjs_wifi_radio_eap_begin_restart', 'wifi_radio_eap_restart_install_locked'):
            code += extract(radio, name)
        compile_run(self, code + RESTART_MAIN)


BOUNDARIES = r'''
#define ESP_ERR_WIFI_NOT_STARTED -10
#define ESP_ERR_WIFI_NOT_CONNECT -11
static int link_result=ESP_ERR_WIFI_NOT_CONNECT,install_error,clear_error;
static bool retain_failure,status_fail,disconnect_pending,helpers_idle=true;
static unsigned installs,clears,disconnects,drains,releases;
static uint64_t binding,next_binding;
static bool config_busy,config_closing;
static struct {esp32_mquickjs_wifi_eap_config_token_t control;uint64_t binding;} s_wifi_eap_stop;
static bool stop_cleanup_ready;
static int wifi_finish_configuration_cleanup(void){
    assert(s_wifi_eap_stop.control.identity);
    if(!stop_cleanup_ready)return ESP_ERR_INVALID_STATE;
    s_wifi_eap_stop.control.identity=0;config_busy=false;return ESP_OK;
}
void esp32_mquickjs_wifi_eap_config_begin_close(void){assert(!locks);config_closing=true;}
void esp32_mquickjs_wifi_eap_config_status(esp32_mquickjs_wifi_eap_config_status_t *r){assert(!locks);*r=(esp32_mquickjs_wifi_eap_config_status_t){.busy=config_busy};}
bool esp32_mquickjs_wifi_eap_config_finish_close(void){assert(!locks && config_closing);return !binding && !config_busy;}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){assert(locks && !critical);memset(ap,0xa5,sizeof(*ap));return link_result;}
esp_err_t esp32_mquickjs_wifi_eap_install(esp32_mquickjs_wifi_eap_profile_t *p,esp32_mquickjs_wifi_eap_install_result_t *r){
    assert(p && locks && !critical);installs++;
    *r=(esp32_mquickjs_wifi_eap_install_result_t){.entered=true,.error=install_error};
    if(!install_error || retain_failure){binding=++next_binding;r->retained=true;r->identity=binding;r->enabled=!install_error;}
    return install_error;
}
esp_err_t esp32_mquickjs_wifi_eap_install_clear(uint64_t id,esp32_mquickjs_wifi_eap_install_result_t *r){
    assert(locks && !critical && id==binding);clears++;
    *r=(esp32_mquickjs_wifi_eap_install_result_t){.error=clear_error};
    if(clear_error)return clear_error; /* Dispatch failure has no SDK snapshot. */
    binding=0;r->entered=true;r->sdk.entered=true;return 0;
}
esp_err_t esp32_mquickjs_wifi_eap_install_status(uint64_t id,esp32_mquickjs_wifi_eap_install_result_t *r){
    assert(locks && !critical && id==binding);
    *r=(esp32_mquickjs_wifi_eap_install_result_t){.entered=!status_fail,.identity=status_fail?0:binding,.retained=!status_fail};
    return status_fail?74:0;
}
static int esp32_mquickjs_wifi_start_disconnect(bool *pending){assert(!locks);disconnects++;*pending=disconnect_pending;return 0;}
static bool esp32_mquickjs_wifi_drain_scan(void){assert(!locks);drains++;return true;}
static void wifi_release_radio_operation(void){assert(!locks);releases++;}
static bool wifi_helpers_idle(bool connected){assert(!connected && !locks);return helpers_idle;}
'''

RESTART_BOUNDARIES = r'''
#define RADIO_EVENTS_IDLE 0
#define ESP_ERR_INVALID_RESPONSE -30
typedef __typeof__(s_radio.configuration) esp32_mquickjs_wifi_radio_config_result_t;
static struct {void *snapshot;} s_config_restart;
static struct {esp32_mquickjs_wifi_radio_lifecycle_t owner;} s_policy_restart;
static bool source_rejected;
static unsigned source_reads;
static int esp_wifi_get_mode(wifi_mode_t *mode){assert(locks&&!critical);*mode=s_radio.effective_mode;return 0;}
static int wifi_radio_record_fault(const char *stage,int error){
    assert(locks&&!critical);s_radio.fault_stage=stage;s_radio.fault_error=error;return error;
}
esp_err_t esp32_mquickjs_wifi_eap_install_restart_source(uint64_t id,
    esp32_mquickjs_wifi_eap_profile_t *profile,esp32_mquickjs_wifi_eap_install_result_t *out){
    assert(locks&&!critical&&profile==(void *)1);++source_reads;
    *out=(esp32_mquickjs_wifi_eap_install_result_t){.enabled=!source_rejected,.retained=true,.identity=id};
    return source_rejected?ESP_ERR_INVALID_STATE:ESP_OK;
}
'''

RESTART_MAIN = r'''
int main(void){
    reset_vendor();s_radio.effective_mode=WIFI_MODE_APSTA;s_radio.event_live=WIFI_MODE_APSTA;
    esp32_mquickjs_wifi_radio_lease_t owners[3]={0};
    wifi_radio_operation_lock();
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_APSTA,&owners[0]));
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&owners[1]));
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&owners[2]));
    wifi_radio_operation_unlock();
    esp32_mquickjs_wifi_eap_install_result_t result;
    esp32_mquickjs_wifi_eap_profile_t *profile=(void *)1;
    assert(!esp32_mquickjs_wifi_radio_eap_install(&owners[0],&owners[1],&owners[2],profile,&result));
    uint64_t original=result.identity;unsigned before=installs;
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};wifi_mode_t mode=WIFI_MODE_NULL;
#define BEGIN(allow) esp32_mquickjs_wifi_radio_eap_begin_restart(original,&owners[0],&owners[1],&owners[2],profile,allow,&token,&mode)
    assert(BEGIN(false)==ESP_ERR_INVALID_STATE && !token.identity && !source_reads);
    link_result=0;assert(BEGIN(true)==ESP_ERR_INVALID_STATE&&!source_reads);link_result=ESP_ERR_WIFI_NOT_CONNECT;
    s_radio.event_live=0;assert(BEGIN(true)==ESP_ERR_INVALID_STATE);s_radio.event_live=WIFI_MODE_APSTA;
    uint32_t saved=s_radio.next_lease_identity;s_radio.next_lease_identity=UINT32_MAX;
    assert(BEGIN(true)==ESP_ERR_NO_MEM);s_radio.next_lease_identity=saved;
    source_rejected=true;assert(BEGIN(true)==ESP_ERR_INVALID_STATE&&!token.identity);source_rejected=false;
    assert(!BEGIN(true)&&token.identity&&mode==WIFI_MODE_APSTA&&installs==before);
    assert(!esp32_mquickjs_wifi_radio_eap_clear_lifecycle(original,&token,&result));
    wifi_radio_operation_lock();
    for(unsigned i=0;i<3;++i)wifi_radio_release_locked(&owners[i]);
    /* Physical rebuild boundary changes driver generation, not the reservation. */
    ++s_radio.generation;
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_APSTA,&owners[0]));
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&owners[1]));
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&owners[2]));
    install_error=93;retain_failure=true;
    assert(wifi_radio_eap_restart_install_locked(profile,owners)==93);
    assert(s_eap_radio.identity>original&&!s_eap_radio.ready&&s_eap_radio.generation==s_radio.generation);
    for(unsigned i=0;i<3;++i){assert(!s_eap_radio.owners[i]);wifi_radio_release_locked(&owners[i]);assert(!owners[i].acquired);}
    wifi_radio_operation_unlock();
    assert(!strcmp(s_radio.configuration.stage,"restart-enterprise-install")&&s_radio.configuration.error==93);
    uint64_t failed=s_eap_radio.identity;esp32_mquickjs_wifi_radio_lifecycle_t wrong=token;++wrong.generation;
    before=clears;
    assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(failed,&wrong,&result)==ESP_ERR_INVALID_STATE&&clears==before);
    assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(original,&token,&result)==ESP_ERR_INVALID_STATE&&clears==before);
    clear_error=94;assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(failed,&token,&result)==94&&s_eap_radio.identity==failed);
    clear_error=0;assert(!esp32_mquickjs_wifi_radio_eap_clear_lifecycle(failed,&token,&result)&&!s_eap_radio.identity);
    before=clears;assert(!esp32_mquickjs_wifi_radio_eap_clear_lifecycle(failed,&token,&result)&&clears==before);
    assert(!locks&&!critical&&s_radio.lifecycle.identity==token.identity);
}
'''

MAIN = r'''
int main(void){
    reset_vendor();s_radio.effective_mode=WIFI_MODE_STA;
    esp32_mquickjs_wifi_radio_lease_t app={0},sta={0},ap={0},other={0};
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&app)==0);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&sta)==0);
    wifi_radio_operation_unlock();
    esp32_mquickjs_wifi_eap_install_result_t result;
    esp32_mquickjs_wifi_eap_profile_t *profile=(void *)1; /* Native storage boundary, never dereferenced here. */
#define INSTALL() esp32_mquickjs_wifi_radio_eap_install(&app,&sta,&ap,profile,&result)
    link_result=0;assert(INSTALL()==ESP_ERR_INVALID_STATE && !installs);
    link_result=88;assert(INSTALL()==88 && !installs);link_result=ESP_ERR_WIFI_NOT_CONNECT;
    s_radio.operation.identity=1;assert(INSTALL()==ESP_ERR_INVALID_STATE && !installs);s_radio.operation.identity=0;
    ++sta.identity;assert(INSTALL()==ESP_ERR_INVALID_STATE && !installs);--sta.identity;
    install_error=71;assert(INSTALL()==71 && !s_eap_radio.identity);
    retain_failure=true;assert(INSTALL()==71 && s_eap_radio.identity && !s_eap_radio.ready);
    uint64_t failed=s_eap_radio.identity;
    wifi_radio_operation_lock();
    wifi_radio_release_locked(&sta);wifi_radio_release_locked(&app);assert(sta.acquired && app.acquired);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,WIFI_MODE_STA,&other)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle={0};
    assert(wifi_radio_begin_lifecycle_locked(&app,&sta,&ap,&lifecycle)==ESP_ERR_INVALID_STATE && !lifecycle.identity);
    assert(wifi_radio_connection_owner_locked(&app,&sta,&ap,WIFI_IF_STA,false)==ESP_ERR_INVALID_STATE);
    wifi_radio_operation_unlock();
    unsigned before=clears;assert(esp32_mquickjs_wifi_radio_eap_clear(failed+1,&result)==ESP_ERR_INVALID_STATE && clears==before);
    clear_error=72;assert(esp32_mquickjs_wifi_radio_eap_clear(failed,&result)==72 && result.retained && result.identity==failed && !result.entered);
    status_fail=true;assert(esp32_mquickjs_wifi_radio_eap_status(failed,&result)==74 && result.retained && result.identity==failed);status_fail=false;
    clear_error=0;assert(esp32_mquickjs_wifi_radio_eap_clear(failed,&result)==0 && !s_eap_radio.identity);
    before=clears;assert(esp32_mquickjs_wifi_radio_eap_clear(failed,&result)==ESP_ERR_INVALID_STATE && clears==before);
    /* APSTA helper identities are pinned together. Normal connection ownership
     * remains admissible because EAP uses no scan/connect operation slot. */
    s_radio.effective_mode=WIFI_MODE_APSTA;
    wifi_radio_operation_lock();assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&ap)==0);wifi_radio_operation_unlock();
    install_error=0;retain_failure=false;assert(INSTALL()==0 && s_eap_radio.ready && !s_radio.operation.identity);
    uint64_t active=s_eap_radio.identity;assert(active>failed);
    wifi_radio_operation_lock();assert(wifi_radio_connection_owner_locked(&app,&sta,&ap,WIFI_IF_STA,false)==0);
    wifi_radio_release_locked(&ap);assert(ap.acquired);wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_eap_clear(failed,&result)==ESP_ERR_INVALID_STATE && s_eap_radio.identity==active);
    /* Exact stop admission transfers pins without an SDK call. */
    before=clears;
    assert(esp32_mquickjs_wifi_radio_eap_begin_stop(active+1,&app,&sta,&ap,&lifecycle)==ESP_ERR_INVALID_STATE);
    s_radio.wake_locks=1;
    assert(esp32_mquickjs_wifi_radio_eap_begin_stop(active,&app,&sta,&ap,&lifecycle)==ESP_ERR_INVALID_STATE);
    s_radio.wake_locks=0;
    esp32_mquickjs_wifi_radio_lease_t wrong=ap;++wrong.identity;
    assert(esp32_mquickjs_wifi_radio_eap_begin_stop(active,&app,&sta,&wrong,&lifecycle)==ESP_ERR_INVALID_STATE);
    assert(!lifecycle.identity && clears==before);
    assert(esp32_mquickjs_wifi_radio_eap_begin_stop(active,&app,&sta,&ap,&lifecycle)==ESP_OK && lifecycle.identity);
    esp32_mquickjs_wifi_radio_lifecycle_t old=lifecycle;++old.identity;
    assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(active,&old,&result)==ESP_ERR_INVALID_STATE && clears==before);
    assert(esp32_mquickjs_wifi_radio_eap_clear(active,&result)==ESP_ERR_INVALID_STATE && clears==before);
    clear_error=81;assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(active,&lifecycle,&result)==81);
    assert(s_eap_radio.identity==active && s_radio.lifecycle.identity==lifecycle.identity && ap.acquired);
    clear_error=0;assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(active,&lifecycle,&result)==ESP_OK);
    before=clears;assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(active,&lifecycle,&result)==ESP_OK && clears==before);
    assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(active,&old,&result)==ESP_ERR_INVALID_STATE && clears==before);
    /* Simulate the subsequent driver/helper suffix releasing its lifecycle;
     * those production bodies are composed by the enterprise-stop fixture. */
    s_radio.lifecycle=(esp32_mquickjs_wifi_radio_lifecycle_t){0};lifecycle=s_radio.lifecycle;
    assert(INSTALL()==0);active=s_eap_radio.identity;
    s_wifi_eap_stop.control.identity=1;config_busy=true;
    before=disconnects;assert(!esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && disconnects==before);
    stop_cleanup_ready=true;
    s_wifi_eap_stop.control.identity=0;config_busy=false;
    /* Runtime keeps helpers and credentials until disconnect AND reuse fences
     * are drained; later SDK clear failure is retried on the original identity. */
    config_busy=true;before=disconnects;assert(!esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && disconnects==before);config_busy=false;
    disconnect_pending=true;before=clears;assert(!esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && clears==before);
    disconnect_pending=false;helpers_idle=false;assert(!esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && clears==before);
    helpers_idle=true;clear_error=73;assert(!esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && s_eap_radio.identity==active);
    clear_error=0;assert(esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && !s_eap_radio.identity && !binding);
    before=disconnects;assert(esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && disconnects==before);
    wifi_radio_operation_lock();wifi_radio_release_locked(&ap);wifi_radio_release_locked(&sta);wifi_radio_release_locked(&app);wifi_radio_operation_unlock();
    assert(!ap.acquired && !sta.acquired && !app.acquired && !locks && !critical && drains && releases);
    return 0;
}
'''
