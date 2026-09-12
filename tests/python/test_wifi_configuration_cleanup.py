"""Production AP/STA coordinator admission and cleanup suffix; phase-run pending."""
import pathlib
import unittest
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract as function

ROOT = pathlib.Path(__file__).resolve().parents[2]
WIFI = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c'
AP = WIFI.with_name('esp32_mquickjs_wifi_ap.c')


class WiFiConfigurationCleanup(unittest.TestCase):
    def code(self):
        wifi, ap = WIFI.read_text(), AP.read_text()
        body = ''.join(function(ap, name) for name in ['wifi_ap_retire_netif',
            'wifi_ap_cleanup', 'esp32_mquickjs_wifi_ap_begin_configuration',
            'esp32_mquickjs_wifi_ap_begin_selected_configuration',
            'wifi_ap_retire_for_token', 'esp32_mquickjs_wifi_ap_retire_for_configuration',
            'esp32_mquickjs_wifi_ap_retire_for_recovery'])
        body += ''.join(function(wifi, name) for name in ['esp32_mquickjs_wifi_connection_reserved_locked', 'wifi_helpers_idle',
            'esp32_mquickjs_wifi_retire_for_recovery',
            'wifi_begin_configuration_cleanup', 'wifi_begin_selected_configuration', 'wifi_finish_configuration_cleanup',
            'esp32_mquickjs_wifi_configuration_pending', 'esp32_mquickjs_wifi_cleanup_ap_configuration'])
        # AP exports are non-static in production; use the shared extractor for them.
        return BOUNDARIES + body

    def test_exact_handoff_retains_token_through_each_failed_suffix(self):
        compile_run(self, self.code() + MAIN)

    def test_recovery_helper_suffix_preserves_lifecycle_until_native_owner_retires(self):
        compile_run(self, self.code() + r'''
int main(void) {
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    s_ap_netif=(void *)1;sta_storage=running=true;
    assert(wifi_begin_configuration_cleanup(false)==ESP_OK);
    /* Inject the admitted Radio identity, not a replacement cleanup FSM. */
    recovery_admitted=recovery_owner_pending=true;
    esp32_mquickjs_wifi_radio_lifecycle_t stale=s_wifi_lifecycle;++stale.identity;
    assert(esp32_mquickjs_wifi_ap_retire_for_recovery(&stale)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_retire_for_recovery(&stale)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_ap_retire_for_recovery(&s_wifi_lifecycle)==ESP_ERR_INVALID_STATE);
    fail_at=2;
    assert(wifi_finish_configuration_cleanup()==-2 && s_ap_netif && sta_storage);
    assert(!recovery_shutdown_calls && stop_calls==1 && s_ap_coordinator.identity==41);
    fail_at=0;
    assert(wifi_finish_configuration_cleanup()==ESP_ERR_TIMEOUT);
    assert(s_wifi_lifecycle.identity==41 && s_wifi_configuration_cleanup && s_wifi_state.runtime_cleanup_pending);
    assert(!strcmp(s_wifi_state.cleanup_stage,"configuration-recovery-shutdown"));
    assert(!s_ap_netif && !sta_storage && !s_ap_coordinator.identity);
    assert(stop_calls==1 && ap_calls==2 && sta_calls==1 && !shutdown_calls);
    assert(wifi_finish_configuration_cleanup()==ESP_ERR_TIMEOUT);
    assert(stop_calls==1 && ap_calls==2 && sta_calls==1 && recovery_shutdown_calls==2);
    recovery_owner_pending=false;
    assert(wifi_finish_configuration_cleanup()==ESP_OK && shutdown_calls==1);
    assert(!s_wifi_lifecycle.identity && !s_wifi_configuration_cleanup && !s_wifi_state.runtime_cleanup_pending);
    assert(stop_calls==1 && ap_calls==2 && sta_calls==1 && recovery_shutdown_calls==3);
    return 0;
}
''')

    def test_vendor_cleanup_failure_keeps_coordinator_before_other_suffixes(self):
        compile_run(self, self.code() + r'''
int main(void) {
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    assert(wifi_begin_configuration_cleanup(false)==ESP_OK);
    unsigned releases=owner_releases;vendor_clear_error=77;
    assert(wifi_finish_configuration_cleanup()==77 && s_wifi_lifecycle.identity==41);
    assert(!strcmp(s_wifi_state.cleanup_stage,"configuration-vendor-ie-clear") && owner_releases==releases);
    assert(!stop_calls && !ap_calls && !sta_calls && !shutdown_calls);
    vendor_clear_error=0;assert(wifi_finish_configuration_cleanup()==ESP_OK && !s_wifi_lifecycle.identity);
    return 0;
}
''')

    def test_allow_disconnect_is_checked_before_any_owner_release(self):
        compile_run(self, self.code() + r'''
int main(void) {
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    s_wifi_state.status.connected=true;
    assert(wifi_begin_configuration_cleanup(false)==ESP_ERR_INVALID_STATE && !begin_calls && !owner_releases);
    s_wifi_state.connect_future_registered=true;
    assert(wifi_begin_configuration_cleanup(true)==ESP_ERR_INVALID_STATE && !begin_calls);
    s_wifi_state.connect_future_registered=false;foreign_owner=true;
    assert(wifi_begin_configuration_cleanup(true)==ESP_ERR_INVALID_STATE && !owner_releases);
    assert(!s_wifi_configuration_cleanup && !s_wifi_configuration_disconnect_pending && s_ap_lease.acquired);
    foreign_owner=false;
    assert(wifi_begin_configuration_cleanup(true)==ESP_OK && owner_releases==1 && s_wifi_configuration_disconnect_pending);
    assert(s_wifi_state.radio_lease.acquired && s_wifi_application.acquired && s_wifi_lifecycle.identity);
    assert(!stop_calls && !ap_calls && !sta_calls && !shutdown_calls);
    return 0;
}
''')

    def test_selected_admission_failure_keeps_all_helpers_and_owners(self):
        compile_run(self, self.code() + r'''
int main(void) {
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    esp32_mquickjs_wifi_radio_configuration_selection_t selection={0};
    s_wifi_state.status.connected=true;
    assert(wifi_begin_selected_configuration(&selection,NULL,NULL)==ESP_ERR_INVALID_STATE && !begin_calls);
    selection.allow_disconnect=true;s_wifi_state.scan_future_registered=true;
    assert(wifi_begin_selected_configuration(&selection,NULL,NULL)==ESP_ERR_INVALID_STATE && !begin_calls);
    s_wifi_state.scan_future_registered=false;selection_rejected=true;
    assert(wifi_begin_selected_configuration(&selection,NULL,NULL)==ESP_ERR_INVALID_STATE && !owner_releases);
    assert(selection_allow_disconnect && !s_wifi_configuration_cleanup && !s_ap_cleanup_pending && !s_wifi_lifecycle.identity);
    selection_rejected=false;
    assert(wifi_begin_selected_configuration(&selection,NULL,NULL)==ESP_OK && owner_releases==1);
    assert(s_wifi_configuration_cleanup && s_ap_cleanup_pending && s_wifi_configuration_mode==WIFI_MODE_AP);
    assert(s_wifi_configuration_disconnect_pending && s_wifi_state.radio_lease.acquired && s_wifi_application.acquired);
    assert(wifi_begin_selected_configuration(&selection,NULL,NULL)==ESP_ERR_INVALID_STATE && owner_releases==1);
    return 0;
}
''')


BOUNDARIES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -20
#define ESP_ERR_TIMEOUT -21
typedef int esp_err_t,wifi_mode_t;
typedef struct {int mode;bool allow_disconnect;} esp32_mquickjs_wifi_radio_configuration_selection_t;
typedef int esp32_mquickjs_wifi_radio_config_controls_t,esp32_mquickjs_wifi_radio_start_controls_t;
static bool selection_rejected,selection_allow_disconnect;
#define WIFI_MODE_NULL 0
#define WIFI_MODE_AP 2
static wifi_mode_t s_wifi_configuration_mode;
bool esp32_mquickjs_wifi_configuration_pending(void);
/* Independent AP/configuration scenarios; shared removal has its own fixture. */
static bool esp32_mquickjs_wifi_ap_stop_pending(void) { return false; }
static int esp32_mquickjs_wifi_stop_ap_shared(bool *handled) { *handled=false;return ESP_OK; }
esp_err_t esp32_mquickjs_wifi_cleanup_ap_configuration(void);
typedef struct { unsigned generation,identity;bool acquired; } esp32_mquickjs_wifi_radio_lease_t;
typedef struct { unsigned generation,identity; } esp32_mquickjs_wifi_radio_lifecycle_t;
static int vendor_clear_error;
static int esp32_mquickjs_wifi_radio_vendor_ie_clear_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(token && token->identity);return vendor_clear_error;
}
static esp32_mquickjs_wifi_radio_lease_t s_wifi_application,s_ap_lease;
static esp32_mquickjs_wifi_radio_lifecycle_t s_wifi_lifecycle,s_ap_lifecycle,s_ap_coordinator,native_token;
static bool s_wifi_configuration_cleanup,s_ap_cleanup_pending,s_wifi_configuration_stop_only,s_wifi_configuration_disconnect_pending;
static bool expect_stop_only;
static bool esp32_mquickjs_wifi_radio_ap_deauth_poll(bool *pending) { *pending=false;return false; }
static int wifi_finish_configuration_disconnect(void) { assert(!s_wifi_configuration_disconnect_pending);return 0; }
static void *s_ap_netif;
static struct {uint32_t generation,identity;} s_ap_raw_owner;
static int s_ap_detach_error;
static const char *s_ap_stage;
static struct {
    esp32_mquickjs_wifi_radio_lease_t radio_lease;
    struct { bool connected; } status;
    bool connect_in_progress,connect_draining,connect_start_active,disconnect_active;
    bool scan_in_progress,scan_draining,scan_results_pending,scan_start_active,scan_stop_active;
    bool connect_future_registered,scan_future_registered,runtime_cleanup_pending;
    const char *cleanup_stage;int cleanup_error;
} s_wifi_state;
static int fail_at,begin_calls,owner_releases,stop_calls,ap_calls,sta_calls,shutdown_calls;
static bool foreign_owner,running,sta_storage;
static bool recovery_admitted,recovery_owner_pending;
static unsigned recovery_shutdown_calls;
static void wifi_lock(void) {}
static void wifi_unlock(void) {}
static void wifi_release_radio_operation(void) {}
static bool exact(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    return t && t->identity && t->identity==native_token.identity && t->generation==native_token.generation;
}
static int esp32_mquickjs_wifi_radio_begin_lifecycle(
    const esp32_mquickjs_wifi_radio_lease_t *app,const esp32_mquickjs_wifi_radio_lease_t *sta,
    const esp32_mquickjs_wifi_radio_lease_t *ap,esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    begin_calls++;assert(app==&s_wifi_application && sta==&s_wifi_state.radio_lease && ap==&s_ap_lease);
    assert(token==&s_wifi_lifecycle && !token->identity);
    if(foreign_owner)return ESP_ERR_INVALID_STATE;
    native_token=(esp32_mquickjs_wifi_radio_lifecycle_t){9,41};*token=native_token;return 0;
}
static int esp32_mquickjs_wifi_radio_begin_configuration_lifecycle(
    const esp32_mquickjs_wifi_radio_lease_t *app,const esp32_mquickjs_wifi_radio_lease_t *sta,
    const esp32_mquickjs_wifi_radio_lease_t *ap,esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    const int *controls,const int *start_controls,esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(!controls && !start_controls);selection_allow_disconnect=selection->allow_disconnect;
    if(selection_rejected)return ESP_ERR_INVALID_STATE;
    int err=esp32_mquickjs_wifi_radio_begin_lifecycle(app,sta,ap,token);
    if(!err)selection->mode=WIFI_MODE_AP;return err;
}
static void esp32_mquickjs_wifi_radio_release(esp32_mquickjs_wifi_radio_lease_t *owner) {
    if(owner->acquired){assert(native_token.identity);owner_releases++;memset(owner,0,sizeof(*owner));}
}
static int esp32_mquickjs_wifi_radio_quiesce_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(exact(token) && !s_wifi_application.acquired && !s_wifi_state.radio_lease.acquired && !s_ap_lease.acquired);
    if(!running)return 0;
    stop_calls++;if(fail_at==1)return -1;running=false;return 0;
}
static int esp32_mquickjs_wifi_radio_check_stopped_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *t,bool cleanup) {
    assert(cleanup);return exact(t) && !running?0:ESP_ERR_INVALID_STATE;
}
/* Recovery admission, physical SDK proof and native owner consumption are
 * Radio boundaries; the real producer is composed in test_wifi_action_recovery. */
static int esp32_mquickjs_wifi_radio_prepare_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(exact(token) && recovery_admitted);return ESP_OK;
}
static bool esp32_mquickjs_wifi_radio_recovery_active(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    return recovery_admitted && exact(token);
}
static int esp32_mquickjs_wifi_radio_check_stopped_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    return recovery_admitted && exact(token) && !running?ESP_OK:ESP_ERR_INVALID_STATE;
}
static int esp32_mquickjs_wifi_radio_stop_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(recovery_admitted);return esp32_mquickjs_wifi_radio_quiesce_lifecycle(token);
}
static int esp32_mquickjs_wifi_radio_shutdown_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(recovery_admitted && exact(token) && !running && !sta_storage && !s_ap_netif);
    ++recovery_shutdown_calls;return recovery_owner_pending?ESP_ERR_TIMEOUT:ESP_OK;
}
static int esp32_mquickjs_wifi_netif_retire(void **netif,int *error) {
    assert(native_token.identity && !running);
    if(*error)return *error;
    if(!*netif)return 0;
    ap_calls++;if(fail_at==2)return -2;
    *netif=NULL;return 0;
}
static int wifi_cleanup_helper(bool finish) {
    assert(!finish && native_token.identity && !running && !s_ap_netif);
    if(!sta_storage)return 0;
    sta_calls++;if(fail_at==3){s_wifi_state.cleanup_stage="netif-detach";return -3;}
    sta_storage=false;memset(&s_wifi_state,0,sizeof(s_wifi_state));return 0;
}
static int esp32_mquickjs_wifi_radio_finish_lifecycle(esp32_mquickjs_wifi_radio_lifecycle_t *token,bool shutdown) {
    assert(exact(token) && shutdown==!expect_stop_only && !running && !sta_storage && !s_ap_netif);
    shutdown_calls++;if(fail_at==4)return -4;
    memset(token,0,sizeof(*token));memset(&native_token,0,sizeof(native_token));return 0;
}
'''

MAIN = r'''
int main(void) {
    for(int stop_only=0;stop_only<=1;stop_only++)for(int stage=1;stage<=4;stage++) {
        expect_stop_only=stop_only;
        s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
        s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
        s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
        s_ap_netif=(void *)1;sta_storage=running=true;
        begin_calls=owner_releases=stop_calls=ap_calls=sta_calls=shutdown_calls=0;
        s_wifi_state.connect_future_registered=true;
        assert(wifi_begin_configuration_cleanup(false)==ESP_ERR_INVALID_STATE && !begin_calls);
        s_wifi_state.connect_future_registered=false;foreign_owner=true;
        assert(wifi_begin_configuration_cleanup(false)==ESP_ERR_INVALID_STATE);
        assert(!owner_releases && s_ap_lease.acquired && !s_ap_coordinator.identity && !s_wifi_configuration_cleanup);
        foreign_owner=false;
        assert(wifi_begin_configuration_cleanup(false)==0 && s_wifi_configuration_cleanup && !s_wifi_configuration_stop_only);
        s_wifi_configuration_stop_only=stop_only;
        assert(begin_calls==2 && owner_releases==1 && !s_ap_lease.acquired && s_ap_coordinator.identity==41);
        assert(wifi_ap_cleanup()==ESP_ERR_INVALID_STATE && !stop_calls);
        esp32_mquickjs_wifi_radio_lifecycle_t stale={9,40};
        assert(esp32_mquickjs_wifi_ap_retire_for_configuration(&stale)==ESP_ERR_INVALID_STATE && s_ap_netif);
        fail_at=stage;
        assert(wifi_finish_configuration_cleanup()==-stage);
        assert(s_wifi_configuration_cleanup && s_wifi_state.runtime_cleanup_pending && s_wifi_lifecycle.identity==41);
        assert(s_wifi_configuration_stop_only==stop_only);
        if(stage==4)assert(!strcmp(s_wifi_state.cleanup_stage,stop_only?"configuration-finish-stop":"configuration-shutdown"));
        assert(owner_releases==3 && begin_calls==2);
        assert(ap_calls==(stage>=2) && sta_calls==(stage>=3) && shutdown_calls==(stage==4));
        if(stage<=2)assert(s_ap_netif && s_ap_coordinator.identity==41);
        else assert(!s_ap_netif && !s_ap_coordinator.identity);
        fail_at=0;
        assert(wifi_begin_configuration_cleanup(false)==0 && begin_calls==2);
        assert(wifi_finish_configuration_cleanup()==0);
        assert(!s_wifi_configuration_cleanup && !s_wifi_state.runtime_cleanup_pending && !s_wifi_lifecycle.identity && !s_wifi_configuration_stop_only);
        assert(!s_wifi_state.cleanup_stage && !s_wifi_state.cleanup_error && !s_ap_netif && !sta_storage);
        assert(stop_calls==1+(stage==1) && ap_calls==1+(stage==2) && sta_calls==1+(stage==3));
        assert(shutdown_calls==1+(stage==4) && owner_releases==3 && begin_calls==2);
        assert(wifi_finish_configuration_cleanup()==ESP_ERR_INVALID_STATE);
    }
    return 0;
}
'''
