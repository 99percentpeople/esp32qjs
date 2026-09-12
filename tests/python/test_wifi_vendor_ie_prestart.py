"""Deferred production pre-start Vendor IE transfer and resume/cleanup ordering.

Uses the real slot operations, registry, start admission, initialize/configure/
resume/finish entry points. Physical START/STOP, helper preparation and general
configuration/policy callbacks are injected boundaries, not RF/runtime proof.
"""
import unittest
from test_wifi_vendor_ie import vendor_code
from test_wifi_config_controls import structure
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def prestart_code(profile, ap):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    declarations = ''.join(structure(header, n) for n in (
        'esp32_mquickjs_wifi_radio_config_result_t', 'esp32_mquickjs_wifi_radio_config_controls_t',
        'esp32_mquickjs_wifi_radio_start_controls_t'))
    code = vendor_code(profile, ap)
    code = code.replace('static struct {\n    int lock;', declarations + 'static struct {\n    int lock;', 1)
    code = code.replace('    wifi_mode_t effective_mode;wifi_storage_t storage;',
                        '    wifi_mode_t effective_mode;wifi_storage_t storage;esp32_mquickjs_wifi_radio_config_result_t configuration;')
    code += BOUNDARIES
    for name in ('wifi_radio_record_fault', 'esp32_mquickjs_wifi_radio_begin_start_lifecycle',
                 'esp32_mquickjs_wifi_radio_initialize_lifecycle', 'esp32_mquickjs_wifi_radio_configure_lifecycle',
                 'wifi_radio_resume_lifecycle_locked', 'esp32_mquickjs_wifi_radio_resume_lifecycle',
                 'esp32_mquickjs_wifi_radio_finish_lifecycle'):
        code += extract(radio, name)
    return code


class WiFiVendorIePrestart(unittest.TestCase):
    def test_handoff_copy_preservation_exact_token_first_start_owners_and_cleanup_failures(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(target=profile, softap=ap):
                    compile_run(self, prestart_code(profile, ap) + MAIN)


BOUNDARIES = r'''
#define ESP_ERR_INVALID_RESPONSE -6
static unsigned mode_queries,starts,stops,shutdowns;
static int start_error,mode_error,verify_error;
static struct {bool captured;struct {bool restore_off;} *snapshot;} s_config_restart;
static struct {esp32_mquickjs_wifi_radio_lifecycle_t owner;wifi_mode_t mode;unsigned completed;struct {unsigned mask;} snapshot;} s_policy_restart;
typedef bool (*esp32_mquickjs_wifi_config_accept_fn)(const wifi_config_t *,const wifi_config_t *);
static int esp_wifi_get_mode(wifi_mode_t *out) {assert(locks && !critical);++mode_queries;*out=s_radio.effective_mode;return mode_error;}
static int wifi_radio_initialize(void) {assert(locks && s_radio.driver_owned && !s_radio.started);return ESP_OK;}
static int wifi_radio_configure_locked(wifi_mode_t mode,wifi_storage_t storage,wifi_config_t *sta,
    esp32_mquickjs_wifi_config_accept_fn sta_accept,wifi_config_t *ap,esp32_mquickjs_wifi_config_accept_fn ap_accept,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,esp32_mquickjs_wifi_radio_config_result_t *result) {
    (void)mode;(void)storage;(void)sta;(void)sta_accept;(void)ap;(void)ap_accept;(void)controls;(void)result;
    assert(!"parked Vendor IE start must not write configuration");return 77;
}
static int esp32_mquickjs_wifi_radio_validate_start_controls(bool start,const esp32_mquickjs_wifi_radio_start_controls_t *c) {(void)start;(void)c;return ESP_OK;}
static bool wifi_radio_restart_configs_ready_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode) {(void)t;(void)mode;return !s_config_restart.captured;}
static int wifi_radio_restart_configs_pre_start_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode) {(void)t;(void)mode;assert(!s_radio.started);return ESP_OK;}
static int wifi_radio_restart_configs_start_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode,esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(locks && !critical && wifi_radio_vendor_ie_start_matches(t) && wifi_radio_lease_valid(lease));
    assert(mode==s_radio.effective_mode);++starts;
    /* This is the physical START boundary. Every enabled IE must already be
     * backed by its new exact registry owner before any first frame can leave. */
    for(unsigned i=0;i<10;++i) if(native_length[i]) {
        wifi_interface_t iface=wifi_radio_vendor_ie_interface((wifi_vendor_ie_type_t)(i/2));
        assert(wifi_radio_lease_valid(&s_vendor_ie.leases[iface]) && !s_vendor_ie.slots[i].pending);
        assert(native_ie[i][0]==221 && native_length[i]==6);
    }
    s_radio.started=s_radio.stop_required=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    return start_error; /* Also allow partial START on failure. */
}
static int wifi_radio_apply_start_controls(const esp32_mquickjs_wifi_radio_start_controls_t *c) {assert(c==NULL);return ESP_OK;}
static int wifi_radio_policy_restart_replay_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,bool post) {(void)t;(void)post;assert(0);return 77;}
static int wifi_radio_restart_configs_post_start_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return verify_error;}
static int wifi_radio_restart_configs_commit_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode) {(void)t;(void)mode;return ESP_OK;}
static void wifi_radio_restart_configs_discard_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;}
static int wifi_radio_stop_locked(void) {assert(locks && !critical);++stops;s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;return ESP_OK;}
static int wifi_radio_shutdown_locked(void) {
    assert(locks && !critical && !s_vendor_ie.start_owner.identity);++shutdowns;
    for(unsigned i=0;i<10;++i)assert(!s_vendor_ie.slots[i].length && !s_vendor_ie.slots[i].pending);
    assert(wifi_radio_stop_locked()==ESP_OK);s_radio.driver_owned=false;return ESP_OK;
}
'''

MAIN = r'''
static esp32_mquickjs_wifi_radio_lifecycle_t token;
static esp32_mquickjs_wifi_radio_lease_t app,sta,ap;
static void setup(wifi_mode_t mode) {
    reset_vendor();s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.effective_mode=mode;s_radio.storage=WIFI_STORAGE_FLASH;
    memset(&token,0,sizeof(token));memset(&app,0,sizeof(app));memset(&sta,0,sizeof(sta));memset(&ap,0,sizeof(ap));
    memset(&s_policy_restart,0,sizeof(s_policy_restart));s_config_restart.captured=false;
    starts=stops=shutdowns=mode_queries=0;start_error=mode_error=verify_error=0;
    if(mode & WIFI_MODE_STA)assert(put(1,0)==ESP_OK);
    if(mode & WIFI_MODE_AP)assert(put(0,1)==ESP_OK);
}
static int begin(wifi_mode_t mode,wifi_storage_t storage) {
    esp32_mquickjs_wifi_radio_configuration_selection_t selection={.start_only=true,.mode_set=true,.mode=mode,.storage_set=true,.storage=storage};
    bool running=true;
    int err=esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,NULL,&selection,&token,&running);
    assert(!running);return err;
}
static int resume(wifi_mode_t mode) {
    return esp32_mquickjs_wifi_radio_resume_lifecycle(&token,mode,true,
        mode & WIFI_MODE_STA ? &app : NULL,mode & WIFI_MODE_STA ? &sta : NULL,mode & WIFI_MODE_AP ? &ap : NULL,NULL);
}
static void cleanup(void) {
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear_lifecycle(&token)==ESP_OK);
    assert(!s_vendor_ie.start_owner.identity);
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==ESP_OK && !token.identity);
}
int main(void) {
    const wifi_mode_t modes[]={WIFI_MODE_STA,WIFI_MODE_AP,WIFI_MODE_APSTA};
    for(unsigned m=0;m<3;++m) {
        wifi_mode_t mode=modes[m];if(!CONFIG_ESP_WIFI_SOFTAP_SUPPORT && (mode & WIFI_MODE_AP))continue;
        setup(mode);unsigned copies=native_calls;
        assert(begin(mode,WIFI_STORAGE_FLASH)==ESP_OK && token.identity);
        esp32_mquickjs_wifi_vendor_ie_status_t status;esp32_mquickjs_wifi_radio_vendor_ie_status(&status);
        assert(status.start_pending && !status.owners && native_calls==copies);
        assert(esp32_mquickjs_wifi_radio_initialize_lifecycle(&token)==ESP_OK);
        esp32_mquickjs_wifi_radio_config_result_t result;
        assert(esp32_mquickjs_wifi_radio_configure_lifecycle(&token,mode,WIFI_STORAGE_FLASH,NULL,NULL,NULL,NULL,NULL,&result)==ESP_OK);
        assert(!result.mutation_attempted && native_calls==copies && !starts);
        assert(resume(mode)==ESP_OK && starts==1 && !token.identity && !s_vendor_ie.start_owner.identity);
        assert(owners()==((mode & WIFI_MODE_STA ? 1U : 0U)+(mode & WIFI_MODE_AP ? 1U : 0U)) && native_calls==copies);
        assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK);
        wifi_radio_operation_lock();wifi_radio_release_locked(&ap);wifi_radio_release_locked(&sta);wifi_radio_release_locked(&app);wifi_radio_operation_unlock();
    }
    setup(WIFI_MODE_STA);unsigned copies=native_calls;
    assert(begin(WIFI_MODE_STA,WIFI_STORAGE_RAM)==ESP_ERR_INVALID_STATE && !token.identity && owners()==1 && native_calls==copies);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(begin(WIFI_MODE_APSTA,WIFI_STORAGE_FLASH)==ESP_ERR_INVALID_STATE && !token.identity && owners()==1);
#endif
    s_radio.next_lease_identity=UINT32_MAX;assert(begin(WIFI_MODE_STA,WIFI_STORAGE_FLASH)==ESP_ERR_NO_MEM && owners()==1 && !token.identity);
    setup(WIFI_MODE_STA);s_vendor_ie.slots[2].pending=true;
    assert(begin(WIFI_MODE_STA,WIFI_STORAGE_FLASH)==ESP_ERR_INVALID_STATE && owners()==1 && !token.identity);
    setup(WIFI_MODE_STA);s_radio.leases[11].identity=99;
    assert(begin(WIFI_MODE_STA,WIFI_STORAGE_FLASH)==ESP_ERR_INVALID_STATE && owners()==1 && !token.identity);
    setup(WIFI_MODE_STA);assert(begin(WIFI_MODE_STA,WIFI_STORAGE_FLASH)==ESP_OK);
    copies=native_calls;esp32_mquickjs_wifi_radio_lifecycle_t old=token;++old.identity;
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear_lifecycle(&old)==ESP_ERR_INVALID_STATE && native_calls==copies);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_ERR_INVALID_STATE && native_calls==copies);
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==ESP_ERR_INVALID_STATE && !shutdowns && token.identity);
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,false)==ESP_ERR_INVALID_STATE && !stops && token.identity);
    fail_call=native_calls+1;assert(esp32_mquickjs_wifi_radio_vendor_ie_clear_lifecycle(&token)==77);
    assert(s_vendor_ie.start_owner.identity && s_vendor_ie.slots[2].pending);
    fail_call=0;cleanup();assert(shutdowns==1);
    for(unsigned failure=0;failure<3;++failure) {
        setup(WIFI_MODE_STA);assert(begin(WIFI_MODE_STA,WIFI_STORAGE_FLASH)==ESP_OK);
        if(failure==0)mode_error=71;
        if(failure==1)start_error=72;
        if(failure==2)verify_error=73;
        assert(resume(WIFI_MODE_STA)==(int)(71+failure));
        assert(token.identity && s_vendor_ie.start_owner.identity && !app.acquired && !sta.acquired && !owners());
        assert(starts==(failure==0 ? 0U : 1U));mode_error=start_error=verify_error=0;cleanup();
    }
    assert(!locks && !critical);return 0;
}
'''
