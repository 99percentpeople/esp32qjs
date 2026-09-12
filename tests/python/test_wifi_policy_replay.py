"""Deferred actual frozen-policy capture/replay, not complete restart/RF proof."""
import re
import unittest
from test_wifi_policy_record import PRELUDE
from test_wifi_driver_policy import policy_code
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiPolicyReplay(unittest.TestCase):
    def test_real_snapshot_phase_order_suffix_failure_false_values_and_revision_space(self):
        code = PRELUDE + unit(COMPONENT / 'internal/esp32_mquickjs_wifi_policy.h')
        code += unit(COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_policy.c')
        compile_run(self, code + CORE_MAIN)

    def test_real_radio_plan_exact_lifecycle_native_phase_rebuild_and_cleanup(self):
        code = policy_code('esp32c5/representative', True, True)
        code = code.replace('struct {unsigned identity;} operation,lifecycle;',
                            'struct {unsigned identity,generation;} operation,lifecycle;')
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        code += re.search(r'static struct \{[^}]*\} s_policy_restart;', radio).group(0)
        code += CLEANUP_BOUNDARIES
        for name in ('wifi_radio_policy_restart_prepare_locked', 'wifi_radio_policy_restart_replay_locked',
                     'esp32_mquickjs_wifi_radio_finish_lifecycle'):
            code += extract(radio, name)
        compile_run(self, code + RADIO_MAIN)

    def test_production_resume_does_not_publish_owner_before_post_start_acceptance(self):
        compile_run(self, resume_code() + RESUME_MAIN)

    def test_enterprise_install_precedes_snapshot_discard_and_owner_publication(self):
        compile_run(self, resume_code(enterprise=True) + ENTERPRISE_RESUME_MAIN)


def resume_code(enterprise=False):
    code = policy_code('esp32c5/representative', True, True)
    code = code.replace('struct {unsigned identity;} operation,lifecycle;',
                        'struct {unsigned identity,generation;} operation,lifecycle;')
    code = code.replace('int lock;uint32_t generation,wake_locks;',
                        'int lock;uint32_t generation,wake_locks,next_lease_identity;')
    code = code.replace('static bool native_dynamic',
                        'static esp32_mquickjs_wifi_radio_lease_t *observed_owner;\nstatic bool native_dynamic')
    code = code.replace('native_dynamic=value;return sdk_step(true);',
                        'assert(!observed_owner || !observed_owner->acquired);native_dynamic=value;return sdk_step(true);')
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code += re.search(r'static struct \{[^}]*\} s_policy_restart;', radio).group(0)
    code += 'struct esp32_mquickjs_wifi_eap_profile;\n'
    code += RESUME_BOUNDARIES
    if enterprise:
        code += ENTERPRISE_RESUME_BOUNDARIES
    for name in ('wifi_radio_policy_restart_prepare_locked', 'wifi_radio_policy_restart_replay_locked',
                 'wifi_radio_resume_lifecycle_locked'):
        code += extract(radio, name)
    return code


CORE_MAIN = r'''
static esp32_mquickjs_wifi_policy_state_t state;
static esp32_mquickjs_wifi_policy_write_t result;
static esp32_mquickjs_wifi_policy_snapshot_t snapshot;
static unsigned calls,seen[4];static int failed_slot=-1;static bool values[4];
static int writer(void *p,esp32_mquickjs_wifi_policy_slot_t slot,bool value) {
    assert(p==&state);++calls;++seen[slot];values[slot]=value;
    assert(result.attempted && state.records[slot].uncertain && !state.records[slot].known);
    return (int)slot==failed_slot ? 77 : ESP_OK;
}
#define REPLAY(gen,after) esp32_mquickjs_wifi_policy_replay(&state,gen,&snapshot,after,&completed,writer,&state,&result)
int main(void) {
    uint8_t completed=0;
    assert(esp32_mquickjs_wifi_policy_capture(&state,7,&snapshot)==ESP_OK && !snapshot.mask);
    assert(REPLAY(7,false)==ESP_OK && !calls); /* Cold start without configured policies. */
    for(unsigned i=0;i<4;++i)assert(esp32_mquickjs_wifi_policy_apply(&state,7,i,(i&1)!=0,writer,&state,&result)==ESP_OK);
    assert(esp32_mquickjs_wifi_policy_capture(&state,8,&snapshot)==ESP_ERR_INVALID_STATE && !snapshot.mask);
    assert(esp32_mquickjs_wifi_policy_capture(&state,7,&snapshot)==ESP_OK && snapshot.mask==15 && snapshot.values==10);
    unsigned before=calls;assert(REPLAY(7,false)==ESP_ERR_INVALID_STATE && calls==before);
    esp32_mquickjs_wifi_policy_invalidate(&state);
    assert(REPLAY(8,true)==ESP_ERR_INVALID_STATE && calls==before); /* Never post before pre. */
    failed_slot=2;
    assert(REPLAY(8,false)==77 && completed==2 && snapshot.values==10 && state.records[2].uncertain);
    unsigned first=seen[1];failed_slot=-1;
    assert(REPLAY(8,false)==ESP_OK && completed==14 && seen[1]==first);
    assert(REPLAY(8,true)==ESP_OK && completed==15 && !values[0] && values[1] && !values[2] && values[3]);
    before=calls;assert(REPLAY(8,true)==ESP_OK && calls==before);
    esp32_mquickjs_wifi_policy_snapshot_t unchanged=snapshot;
    failed_slot=1;
    assert(esp32_mquickjs_wifi_policy_apply(&state,8,1,false,writer,&state,&result)==77);
    assert(esp32_mquickjs_wifi_policy_capture(&state,8,&snapshot)==ESP_ERR_INVALID_STATE && !snapshot.mask);
    snapshot=unchanged;esp32_mquickjs_wifi_policy_invalidate(&state);completed=0;failed_slot=-1;
    assert(REPLAY(9,false)==ESP_OK && REPLAY(9,true)==ESP_OK); /* Original frozen intent survives failed replay generation. */
    assert(!values[0] && values[1] && !values[2] && values[3]);
    state.revision=UINT32_MAX-3;
    assert(esp32_mquickjs_wifi_policy_capture(&state,9,&snapshot)==ESP_ERR_INVALID_STATE && !snapshot.mask);
    state.revision=UINT32_MAX-4;
    assert(esp32_mquickjs_wifi_policy_capture(&state,9,&snapshot)==ESP_OK);
    esp32_mquickjs_wifi_policy_invalidate(&state);completed=0;
    assert(REPLAY(10,false)==ESP_OK && REPLAY(10,true)==ESP_OK && state.revision==UINT32_MAX);
    assert(esp32_mquickjs_wifi_policy_capture(&state,10,&snapshot)==ESP_ERR_INVALID_STATE);
    return 0;
}
'''

CLEANUP_BOUNDARIES = r'''
/* Isolated no-Vendor-IE module boundary. */
#define WIFI_RADIO_MESH_PENDING false
static struct {esp32_mquickjs_wifi_radio_lifecycle_t start_owner;} s_vendor_ie;
static void wifi_radio_restart_configs_discard_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {(void)token;} /* Config checkpoint covered separately. */
static int cleanup_error;static unsigned cleanups;
static int wifi_radio_shutdown_locked(void) {assert(locks==1 && !critical);++cleanups;return cleanup_error;}
static int wifi_radio_stop_locked(void) {assert(locks==1 && !critical);++cleanups;return cleanup_error;}
'''

RADIO_MAIN = r'''
int main(void) {
    policy_reset(true);
    esp32_mquickjs_wifi_radio_lifecycle_t token={.generation=7,.identity=41},old=token;
    s_radio.lifecycle.identity=token.identity;s_radio.lifecycle.generation=token.generation;
    wifi_radio_operation_lock();
    esp32_mquickjs_wifi_policy_write_t write;
    for(unsigned i=0;i<4;++i)assert(esp32_mquickjs_wifi_policy_apply(&s_policies,7,i,i==1,wifi_radio_policy_writer,NULL,&write)==ESP_OK);
    assert(wifi_radio_policy_restart_prepare_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && !s_policy_restart.owner.identity);
    assert(wifi_radio_policy_restart_prepare_locked(&token,WIFI_MODE_APSTA)==ESP_OK);
    assert(s_policy_restart.snapshot.mask==15 && s_policy_restart.snapshot.values==2);
    old.identity++;unsigned before=calls;
    assert(wifi_radio_policy_restart_replay_locked(&old,false)==ESP_ERR_INVALID_STATE && calls==before);
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_ERR_INVALID_STATE && calls==before);
    /* Native physical deinit and subsequent init are injected boundaries here.
     * Frozen plan, ledger, phase admission and cleanup coordinator are real. */
    esp32_mquickjs_wifi_policy_invalidate(&s_policies);s_radio.generation=8;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;s_radio.fault_stage=NULL;s_radio.fault_error=0;
    fail_at=calls+2;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==77 && s_policy_restart.completed==2);
    assert(!strcmp(s_radio.fault_stage,"restart-policy-pre-start") && s_policy_restart.owner.identity==41);
    assert(wifi_radio_policy_restart_prepare_locked(&token,WIFI_MODE_APSTA)==ESP_OK); /* Does not recapture uncertain output. */
    fail_at=0;before=calls;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_ERR_INVALID_STATE && calls==before);
    esp32_mquickjs_wifi_policy_invalidate(&s_policies);s_radio.generation=9;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;s_radio.fault_stage=NULL;s_radio.fault_error=0;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK && s_policy_restart.completed==14 && calls==before+3);
    before=calls;
    assert(wifi_radio_policy_restart_replay_locked(&token,true)==ESP_ERR_INVALID_STATE && calls==before);
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_policy_restart_replay_locked(&token,true)==ESP_OK && s_policy_restart.completed==15 && !native_dynamic && native_11b[0] && !native_11b[1] && !native_coex);
    wifi_radio_operation_unlock();
    cleanup_error=77;
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==77 && token.identity==41 && s_policy_restart.owner.identity==41);
    cleanup_error=0;
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==ESP_OK && !token.identity && !s_policy_restart.owner.identity && cleanups==2);
    assert(!locks && !critical && !helper_locks);
    return 0;
}
'''


RESUME_BOUNDARIES = r'''
/* Vendor IE handoff is covered by test_wifi_vendor_ie_prestart. */
static struct {esp32_mquickjs_wifi_radio_lifecycle_t start_owner;wifi_mode_t start_mode;wifi_storage_t start_storage;} s_vendor_ie;
static bool wifi_radio_vendor_ie_start_matches(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return false;}
static unsigned wifi_radio_vendor_ie_start_count(void) {return 0;}
static int wifi_radio_vendor_ie_start_attach(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return ESP_OK;}
static void wifi_radio_vendor_ie_start_park(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;}
static void wifi_radio_vendor_ie_start_commit(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;}

typedef struct {bool restore_off;} resume_config_snapshot_t;
static struct {bool captured;resume_config_snapshot_t *snapshot;} s_config_restart;
static bool wifi_radio_restart_configs_ready_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token,wifi_mode_t mode) {(void)token;(void)mode;return true;}
static bool config_committed;static unsigned config_discards;
static int config_verify_error;
static int config_power_error;
static int config_pre_start_error;
/* The production checkpoint implementation is covered by restart_configs.
 * This boundary injects its outcome into the actual resume ordering. */
static int wifi_radio_restart_configs_pre_start_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token,wifi_mode_t mode) {
    (void)token;(void)mode;assert(!s_radio.started && !observed_owner->acquired);return config_pre_start_error;
}
static int wifi_radio_restart_configs_post_start_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {(void)token;assert(!observed_owner->acquired);return config_power_error;}
static int wifi_radio_restart_configs_commit_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token,wifi_mode_t mode) {(void)token;(void)mode;config_committed=config_verify_error==0;return config_verify_error;}
static void wifi_radio_restart_configs_discard_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {(void)token;++config_discards;}
#define ESP_ERR_NO_MEM 17
typedef struct {bool tx_power_set;int8_t tx_power_quarter_dbm;} esp32_mquickjs_wifi_radio_start_controls_t;
static int esp32_mquickjs_wifi_radio_validate_start_controls(bool start,const esp32_mquickjs_wifi_radio_start_controls_t *c) {(void)start;assert(c==NULL);return ESP_OK;}
static int wifi_radio_apply_start_controls(const esp32_mquickjs_wifi_radio_start_controls_t *c) {assert(c==NULL);return ESP_OK;}
static int esp_wifi_get_mode(wifi_mode_t *mode) {assert(locks==1 && !critical);*mode=s_radio.effective_mode;return ESP_OK;}
static unsigned acquired,released;
static int wifi_radio_acquire_locked(esp32_mquickjs_wifi_radio_client_t client,wifi_mode_t mode,esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(locks==1 && mode==WIFI_MODE_STA);++acquired;
    *lease=(esp32_mquickjs_wifi_radio_lease_t){s_radio.generation,s_radio.next_lease_identity++,client,true};
    s_radio.leases[0]=(wifi_radio_live_lease_t){lease->identity,client};return ESP_OK;
}
static int wifi_radio_ensure_started_locked(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(locks==1 && lease->acquired);s_radio.started=s_radio.stop_required=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;return ESP_OK;
}
static int wifi_radio_restart_configs_start_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token,wifi_mode_t mode,esp32_mquickjs_wifi_radio_lease_t *lease) {
    (void)token;assert(mode==WIFI_MODE_STA && !observed_owner->acquired);return wifi_radio_ensure_started_locked(lease);
}
static void wifi_radio_release_locked(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(locks==1);if(!lease->acquired)return;++released;s_radio.leases[0].identity=0;memset(lease,0,sizeof(*lease));
}
'''

RESUME_MAIN = r'''
int main(void) {
    policy_reset(true);s_radio.effective_mode=WIFI_MODE_STA;s_radio.next_lease_identity=1;
    esp32_mquickjs_wifi_radio_lifecycle_t token={.generation=7,.identity=41};
    s_radio.lifecycle.generation=7;s_radio.lifecycle.identity=41;
    esp32_mquickjs_wifi_radio_lease_t output={0};observed_owner=&output;
    wifi_radio_operation_lock();esp32_mquickjs_wifi_policy_write_t write;
    assert(esp32_mquickjs_wifi_policy_apply(&s_policies,7,0,true,wifi_radio_policy_writer,NULL,&write)==ESP_OK);
    assert(wifi_radio_policy_restart_prepare_locked(&token,WIFI_MODE_STA)==ESP_OK);
    esp32_mquickjs_wifi_policy_invalidate(&s_policies);s_radio.generation=8;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK);
    config_pre_start_error=64;
    unsigned identity=s_radio.next_lease_identity;
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,false,NULL)==64);
    assert(!acquired && !released && !s_radio.started && !output.acquired && token.identity==41);
    assert(s_radio.next_lease_identity==identity && s_policy_restart.owner.identity==41);
    config_pre_start_error=0;
    fail_at=calls+1;
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,false,NULL)==77);
    assert(!output.acquired && !output.identity && token.identity==41 && s_policy_restart.owner.identity==41);
    assert(acquired==1 && released==1 && !s_radio.leases[0].identity && s_radio.started);
    /* Inject successful physical cleanup/init into a new generation. */
    esp32_mquickjs_wifi_policy_invalidate(&s_policies);s_radio.generation=9;
    s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.fault_stage=NULL;s_radio.fault_error=0;fail_at=0;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK);
    config_verify_error=66;
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,false,NULL)==66);
    assert(!output.acquired && token.identity==41 && s_policy_restart.owner.identity==41);
    assert(acquired==2 && released==2); /* Final config readback/storage commit also precede publication. */
    esp32_mquickjs_wifi_policy_invalidate(&s_policies);s_radio.generation=10;
    s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    config_verify_error=0;config_power_error=65;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK);
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,false,NULL)==65);
    assert(!output.acquired && token.identity==41 && s_policy_restart.owner.identity==41);
    assert(acquired==3 && released==3); /* Power restore failure never publishes. */
    esp32_mquickjs_wifi_policy_invalidate(&s_policies);s_radio.generation=11;
    s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    config_power_error=0;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK);
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,false,NULL)==ESP_OK);
    assert(output.acquired && output.generation==11 && !token.identity && !s_policy_restart.owner.identity);
    assert(s_policies.records[0].known && s_policies.records[0].generation==11 && native_dynamic);
    wifi_radio_release_locked(&output);
    s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    token=(esp32_mquickjs_wifi_radio_lifecycle_t){.generation=11,.identity=42};
    s_radio.lifecycle.identity=42;s_radio.lifecycle.generation=11;
    /* Actual resume validates the off checkpoint before any START. */
    unsigned prior_acquired=acquired;
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,true,NULL)==ESP_ERR_INVALID_STATE);
    assert(acquired==prior_acquired && !s_radio.started && !output.acquired && token.identity==42);
    resume_config_snapshot_t off_snapshot={true};
    s_config_restart.snapshot=&off_snapshot;s_config_restart.captured=true;
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,false,NULL)==ESP_ERR_INVALID_STATE);
    assert(acquired==prior_acquired && !s_radio.started && token.identity==42);
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,true,NULL)==ESP_OK);
    assert(output.acquired && token.identity==42 && s_radio.lifecycle.identity==42 && s_config_restart.snapshot);
    wifi_radio_release_locked(&output);s_config_restart.snapshot=NULL;s_config_restart.captured=false;
    wifi_radio_operation_unlock();assert(!locks && !critical && !helper_locks);
    return 0;
}
'''


ENTERPRISE_RESUME_BOUNDARIES = r'''
#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1
static unsigned enterprise_installs;static int enterprise_error;
static int wifi_radio_eap_restart_install_locked(struct esp32_mquickjs_wifi_eap_profile *profile,
    const esp32_mquickjs_wifi_radio_lease_t owners[3]) {
    assert(locks==1&&!critical&&profile==(void *)1&&config_committed&&!config_discards);
    assert(s_radio.lifecycle.identity&&s_radio.started&&!observed_owner->acquired);
    assert(owners[0].acquired&&owners[1].acquired&&!owners[2].acquired);
    ++enterprise_installs;return enterprise_error;
}
'''

ENTERPRISE_RESUME_MAIN = r'''
int main(void){
    policy_reset(true);s_radio.effective_mode=WIFI_MODE_STA;s_radio.next_lease_identity=1;
    esp32_mquickjs_wifi_radio_lifecycle_t token={.generation=7,.identity=41};
    s_radio.lifecycle.generation=7;s_radio.lifecycle.identity=41;
    esp32_mquickjs_wifi_radio_lease_t app={0},sta={0};observed_owner=&app;
    struct esp32_mquickjs_wifi_eap_profile *profile=(void *)1;
    wifi_radio_operation_lock();
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&app,&sta,NULL,NULL,true,profile)==ESP_ERR_INVALID_ARG);
    assert(!enterprise_installs&&!acquired&&!config_discards);
    config_verify_error=91;
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&app,&sta,NULL,NULL,false,profile)==91);
    assert(!enterprise_installs&&!config_discards&&!app.acquired&&!sta.acquired&&token.identity);
    config_verify_error=0;s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    enterprise_error=92;
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&app,&sta,NULL,NULL,false,profile)==92);
    assert(enterprise_installs==1&&!config_discards&&!app.acquired&&!sta.acquired&&token.identity);
    assert(acquired==released); /* Staged owners stay private on EAP failure. */
    enterprise_error=0;s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(!wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&app,&sta,NULL,NULL,false,profile));
    assert(enterprise_installs==2&&config_discards==1&&app.acquired&&sta.acquired&&!token.identity&&!s_radio.lifecycle.identity);
    wifi_radio_release_locked(&sta);wifi_radio_release_locked(&app);wifi_radio_operation_unlock();
    assert(acquired==released&&!locks&&!critical);
}
'''
