"""Deferred production retry admission and immutable checkpoint identity.

Native driver state, feature admission and lock-entry changes are injected.
The exact checkpoint predicate and retry admission are production code. No
SDK calls exist in this fixture; runtime phase ordering is covered separately.
"""
import re
import unittest

from test_wifi_config_controls import PRELUDE, HEADER, RADIO, sdk_types, structure
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiRestartRetry(unittest.TestCase):
    def test_complete_checkpoint_fault_ownership_capacity_and_off_consent(self):
        header, radio = HEADER.read_text(), RADIO.read_text()
        declarations = '#undef ESP32_MQUICKJS_WIFI_RADIO_STOPPED\n'
        declarations += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_driver_state_t;', header).group(0)
        declarations += ''.join(structure(header, name) for name in (
            'esp32_mquickjs_wifi_radio_lifecycle_t', 'esp32_mquickjs_wifi_radio_restart_selection_t',
            'esp32_mquickjs_wifi_radio_config_result_t'))
        limits = '#undef WIFI_RADIO_MAX_LEASES\n' + re.search(r'^#define WIFI_RADIO_MAX_LEASES .*$', radio, re.M).group(0) + '\n'
        functions = ''.join(extract(radio, name) for name in (
            'wifi_radio_restart_checkpoint_matches_locked', 'esp32_mquickjs_wifi_radio_admit_restart_retry'))
        for profile in ('esp32c3', 'esp32s3', 'esp32c5'):
            for ap in (0, 1):
                with self.subTest(target=profile, softap=ap):
                    code = PRELUDE + limits + f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {ap}\n'
                    code += f'#define CONFIG_SOC_WIFI_HE_SUPPORT {int(profile=="esp32c5")}\n'
                    code += f'#define CONFIG_IDF_TARGET_ESP32C5 {int(profile=="esp32c5")}\n'
                    variant = profile + ('/representative-psram' if profile=='esp32s3' else '/representative')
                    compile_run(self, code + sdk_types(variant) + declarations + BOUNDARIES + functions + MAIN)


BOUNDARIES = r'''
#define CONFIG_ESP_WIFI_FTM_ENABLE 1
#define CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT 1
static struct {
    int lock;
    unsigned generation,wake_locks,next_lease_identity;
    bool driver_owned,storage_configured,started,stop_required,restart_required,promiscuous_claimed;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    struct {unsigned identity;} operation,leases[WIFI_RADIO_MAX_LEASES];
    const char *fault_stage,*cleanup_stage;int fault_error,cleanup_error;
    esp32_mquickjs_wifi_radio_config_result_t configuration;
} s_radio;
typedef struct {bool restore_off;int capture_error;} frozen_t;
static frozen_t frozen;
static struct {
    bool captured;frozen_t *snapshot;unsigned source_generation;
    wifi_mode_t mode;esp32_mquickjs_wifi_radio_lifecycle_t owner;
} s_config_restart;
static struct {wifi_mode_t mode;esp32_mquickjs_wifi_radio_lifecycle_t owner;} s_policy_restart;
static struct {unsigned identity;bool restore_pending;} s_tx_rate_lease;
static struct {struct {unsigned identity;} owner;bool restore_pending;} s_interval;
static struct {struct {unsigned identity;} start_owner;} s_vendor_ie;
static bool native_pending[6],recovery[4];
#define WIFI_RADIO_SMARTCONFIG_PENDING native_pending[0]
#define WIFI_RADIO_WPS_PENDING native_pending[1]
#define WIFI_RADIO_DPP_PENDING native_pending[2]
#define WIFI_RADIO_EAP_PENDING native_pending[3]
#define WIFI_RADIO_NAN_PENDING native_pending[4]
#define WIFI_RADIO_MESH_PENDING native_pending[5]
static bool wifi_radio_raw_tx_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return recovery[0];}
static bool wifi_radio_action_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return recovery[1];}
static bool wifi_radio_ftm_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return recovery[2];}
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
static bool wifi_radio_twt_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return recovery[3];}
#endif
static unsigned locked,critical,lock_calls;
static void (*on_lock)(void);
static void wifi_radio_operation_lock(void) {
    assert(!locked && !critical);locked=1;++lock_calls;
    if(on_lock){void (*change)(void)=on_lock;on_lock=NULL;change();}
}
static void wifi_radio_operation_unlock(void) {assert(locked && !critical);locked=0;}
#define taskENTER_CRITICAL(p) do {(void)(p);assert(locked && !critical);critical=1;} while(0)
#define taskEXIT_CRITICAL(p) do {(void)(p);assert(locked && critical);critical=0;} while(0)
static esp32_mquickjs_wifi_radio_lifecycle_t token;
static void setup(void) {
    assert(!locked && !critical);memset(&s_radio,0,sizeof(s_radio));
    memset(&s_config_restart,0,sizeof(s_config_restart));memset(&s_policy_restart,0,sizeof(s_policy_restart));
    memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));memset(&s_interval,0,sizeof(s_interval));
    memset(&s_vendor_ie,0,sizeof(s_vendor_ie));memset(native_pending,0,sizeof(native_pending));
    memset(recovery,0,sizeof(recovery));memset(&frozen,0,sizeof(frozen));on_lock=NULL;lock_calls=0;
    token=(esp32_mquickjs_wifi_radio_lifecycle_t){.identity=41,.generation=7};
    s_radio.lifecycle=token;s_radio.generation=9;s_radio.next_lease_identity=101;
    s_radio.driver_owned=s_radio.storage_configured=s_radio.started=s_radio.stop_required=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_FAULTED;
    s_radio.fault_stage="start-events";s_radio.fault_error=77;
    s_radio.cleanup_stage="stop-events";s_radio.cleanup_error=88;
    s_radio.configuration=(esp32_mquickjs_wifi_radio_config_result_t){.stage="old-config",.error=66};
    s_config_restart.owner=s_policy_restart.owner=token;
    s_config_restart.mode=s_policy_restart.mode=WIFI_MODE_STA;
    s_config_restart.captured=true;s_config_restart.source_generation=7;s_config_restart.snapshot=&frozen;
}
'''


MAIN = r'''
static void reject(int error) {
    esp32_mquickjs_wifi_radio_restart_selection_t selection={.mode=99,.cold=true,.restore_off=true};
    esp32_mquickjs_wifi_radio_lifecycle_t original=token;
    assert(esp32_mquickjs_wifi_radio_admit_restart_retry(&token,&selection)==error);
    assert(!memcmp(&original,&token,sizeof(token)) && !selection.mode && !selection.cold && !selection.restore_off);
    assert(!locked && !critical && !strcmp(s_radio.configuration.stage,"old-config"));
    assert(!strcmp(s_radio.fault_stage,"start-events") && s_radio.fault_error==77);
}
static void late_owner(void) {s_radio.leases[WIFI_RADIO_MAX_LEASES-1].identity=71;}
static void late_checkpoint(void) {s_config_restart.owner.identity++;}
static void accept(bool consent) {
    esp32_mquickjs_wifi_radio_restart_selection_t selection={.allow_ap_restart=consent};
    unsigned identity=s_radio.next_lease_identity;
    assert(!esp32_mquickjs_wifi_radio_admit_restart_retry(&token,&selection));
    assert(selection.mode==s_config_restart.mode && selection.cold==(s_config_restart.snapshot==NULL));
    assert(selection.restore_off==(s_config_restart.snapshot && frozen.restore_off));
    assert(selection.allow_ap_restart==consent && token.identity==41 && token.generation==7);
    assert(s_radio.lifecycle.identity==41 && s_radio.next_lease_identity==identity && s_config_restart.captured);
    assert(!s_radio.configuration.stage && !s_radio.configuration.error);
    assert(!strcmp(s_radio.fault_stage,"start-events") && s_radio.fault_error==77);
    assert(!strcmp(s_radio.cleanup_stage,"stop-events") && s_radio.cleanup_error==88 && !locked && !critical);
}
int main(void) {
    setup();accept(false);
    setup();s_config_restart.snapshot=NULL;accept(false); /* Retained complete cold defaults. */
    setup();s_radio.driver_owned=s_radio.storage_configured=s_radio.started=s_radio.stop_required=false;
    accept(false); /* Known pre-init event/handler failure after original deinit. */
    setup();s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.started=s_radio.stop_required=false;accept(false);
    setup();s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING;accept(false);
#define REJECT_WITH(change) do {setup();change;reject(ESP_ERR_INVALID_STATE);} while(0)
    REJECT_WITH(s_config_restart.captured=false);REJECT_WITH(frozen.capture_error=77);
    REJECT_WITH(s_config_restart.source_generation=0);REJECT_WITH(s_config_restart.owner.identity++);
    REJECT_WITH(s_config_restart.owner.generation++);REJECT_WITH(s_policy_restart.owner.identity++);
    REJECT_WITH(s_policy_restart.owner.generation++);REJECT_WITH(s_policy_restart.mode=WIFI_MODE_AP);
    REJECT_WITH(s_config_restart.mode=s_policy_restart.mode=WIFI_MODE_NULL);
    REJECT_WITH(token.generation++);REJECT_WITH(s_radio.lifecycle.identity++);
    REJECT_WITH(s_radio.restart_required=true);REJECT_WITH(s_radio.operation.identity=7);
    REJECT_WITH(s_radio.wake_locks=1);REJECT_WITH(s_radio.promiscuous_claimed=true);
    REJECT_WITH(s_tx_rate_lease.identity=7);REJECT_WITH(s_tx_rate_lease.restore_pending=true);
    REJECT_WITH(s_interval.owner.identity=7);REJECT_WITH(s_interval.restore_pending=true);
    REJECT_WITH(s_vendor_ie.start_owner.identity=7);
    REJECT_WITH(s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_INITIALIZING);
    REJECT_WITH(s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTING);
    REJECT_WITH(s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPING);
    REJECT_WITH(s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED);
    REJECT_WITH(s_radio.driver_owned=false);REJECT_WITH(on_lock=late_owner);REJECT_WITH(on_lock=late_checkpoint);
    for(unsigned slot=0;slot<WIFI_RADIO_MAX_LEASES;++slot)REJECT_WITH(s_radio.leases[slot].identity=9);
    for(unsigned i=0;i<6;++i)REJECT_WITH(native_pending[i]=true);
    for(unsigned i=0;i<(CONFIG_SOC_WIFI_HE_SUPPORT?4U:3U);++i)REJECT_WITH(recovery[i]=true);
    setup();s_radio.next_lease_identity=0;reject(ESP_ERR_NO_MEM);
    setup();s_radio.next_lease_identity=UINT32_MAX;reject(ESP_ERR_NO_MEM);
    setup();s_radio.next_lease_identity=UINT32_MAX-1;accept(false);
    setup();frozen.restore_off=true;s_radio.next_lease_identity=UINT32_MAX;accept(false);
    setup();s_config_restart.mode=s_policy_restart.mode=WIFI_MODE_AP;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    s_radio.next_lease_identity=UINT32_MAX;accept(false);
    setup();s_config_restart.mode=s_policy_restart.mode=WIFI_MODE_APSTA;
    s_radio.next_lease_identity=UINT32_MAX-1;reject(ESP_ERR_NO_MEM);
    setup();s_config_restart.mode=s_policy_restart.mode=WIFI_MODE_APSTA;
    s_radio.next_lease_identity=UINT32_MAX-2;accept(false);
    setup();s_config_restart.mode=s_policy_restart.mode=WIFI_MODE_APSTA;frozen.restore_off=true;
    reject(ESP_ERR_INVALID_STATE);accept(true);
#else
    reject(ESP_ERR_NOT_SUPPORTED);
#endif
    setup();esp32_mquickjs_wifi_radio_restart_selection_t selection={.mode=99};
    assert(esp32_mquickjs_wifi_radio_admit_restart_retry(NULL,&selection)==ESP_ERR_INVALID_ARG && selection.mode==99);
    assert(esp32_mquickjs_wifi_radio_admit_restart_retry(&token,NULL)==ESP_ERR_INVALID_ARG);
    token.identity=0;assert(esp32_mquickjs_wifi_radio_admit_restart_retry(&token,&selection)==ESP_ERR_INVALID_ARG);
    assert(!lock_calls && selection.mode==99);
    return 0;
}
'''
