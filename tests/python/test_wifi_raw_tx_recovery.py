"""Deferred production Raw TX Radio physical recovery and exact owner retention.

Uses the existing production Radio registry/STOP/shutdown paths. Broker snapshots,
SDK completion/barriers and the rate ledger storage are injected boundaries; real
broker and rate ledger implementations have separate fixtures. No import, compile
or execution while the Wi-Fi API implementation phase remains open.
"""
import re
import unittest
from test_wifi_action_recovery import recovery_code as action_code
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract
from test_wifi_recovery_runtime import recovery_request_code


def recovery_code(profile, ap):
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = action_code(profile, ap)
    code = '#define CONFIG_IDF_TARGET_' + profile.split('/')[0].upper() + ' 1\n' + code
    code = code.replace('struct {unsigned identity,generation;bool restore_pending;} s_tx_rate_lease;',
                        'struct {unsigned identity,generation,write_identity;int interface,previous;bool restore_pending;} s_tx_rate_lease;')
    code = code.replace('static unsigned s_tx_rates,s_policies;', '''static unsigned s_policies;
        typedef struct {bool known,uncertain;unsigned generation,write_identity;} esp32_mquickjs_wifi_tx_rate_record_t;
        static struct {esp32_mquickjs_wifi_tx_rate_record_t records[2];} s_tx_rates;
        static bool esp32_mquickjs_wifi_tx_rate_valid(const int *rate) {return *rate==11;}''')
    code = re.sub(r'static struct \{struct \{unsigned generation,identity,radio_lease_identity;\} operation;bool stopped,sdk_fenced;\} s_raw_tx_recovery;',
        'typedef struct {unsigned generation,identity,radio_lease_identity;} esp32_mquickjs_wifi_raw_tx_token_t;\n' +
        re.search(r'static struct \{\n    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;[^}]*\} s_raw_tx_recovery;', source).group(0), code)
    for name in ('wifi_radio_raw_tx_recovery_exact_locked','wifi_radio_raw_tx_recovery_owner_locked',
                 'esp32_mquickjs_wifi_raw_tx_broker_quiesce'):
        body=extract(code,name)
        code=code.replace(body,body[:body.index('{')].rstrip()+';\n')
    code = code.replace('typedef struct {uint32_t generation;} esp32_mquickjs_wifi_raw_tx_broker_status_t;', '''typedef struct {
        uint32_t generation;esp32_mquickjs_wifi_raw_tx_token_t token;
        bool operation_active,submit_returned,control_busy,native_terminated;
    } esp32_mquickjs_wifi_raw_tx_broker_status_t;
    static esp32_mquickjs_wifi_raw_tx_broker_status_t raw_native;
    static unsigned raw_quiesces;static bool raw_unregistered;static int raw_unregister_error;
    ''')
    code = code.replace('*out=(esp32_mquickjs_wifi_raw_tx_broker_status_t){0};', '*out=raw_native;')
    code = code.replace('assert(locks && !critical && s_action.lease.acquired);', 'assert(locks && !critical && raw_native.token.identity);')
    code = code.replace('assert(s_action.lease.acquired && s_radio.operation.identity);', 'assert(raw_unregistered && s_raw_tx_recovery.sdk_fenced && raw_native.token.identity);')
    body=extract(code,'esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit')
    code=code.replace(body,'''static bool esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(unsigned generation) {
        assert(locks && !critical && deinit_calls && !s_radio.driver_owned && generation==raw_native.token.generation);
        raw_native.operation_active=false;raw_native.native_terminated=true;raw_native.generation=0;return true;
    }''')
    code += BOUNDARIES
    for name in ('wifi_radio_raw_tx_unpin','esp32_mquickjs_wifi_radio_raw_tx_retire',
                 'wifi_radio_raw_tx_recovery_exact_locked','wifi_radio_raw_tx_recovery_owner_locked',
                 'esp32_mquickjs_wifi_radio_raw_tx_recovery_active','esp32_mquickjs_wifi_radio_begin_raw_tx_recovery',
                 'wifi_radio_raw_tx_recovery_phase','esp32_mquickjs_wifi_radio_stop_raw_tx_recovery',
                 'esp32_mquickjs_wifi_radio_shutdown_raw_tx_recovery','esp32_mquickjs_wifi_radio_check_stopped_raw_tx_recovery',
                 'esp32_mquickjs_wifi_radio_finish_raw_tx_recovery'):
        code += extract(source,name)
    code += recovery_request_code(ftm=False)
    for name in ('esp32_mquickjs_wifi_radio_begin_recovery','esp32_mquickjs_wifi_radio_recovery_active',
                 'esp32_mquickjs_wifi_radio_stop_recovery','esp32_mquickjs_wifi_radio_shutdown_recovery',
                 'esp32_mquickjs_wifi_radio_check_stopped_recovery','esp32_mquickjs_wifi_radio_finish_recovery'):
        code += extract(source,name)
    return code


class WiFiRawTxRecovery(unittest.TestCase):
    def test_exact_admission_stop_barriers_physical_termination_and_owner_release(self):
        for profile in ('esp32c3/representative','esp32s3/representative-psram','esp32c5/representative'):
            for ap in (False,True):
                with self.subTest(profile=profile,ap=ap):
                    compile_run(self,recovery_code(profile,ap)+MAIN)


BOUNDARIES = r'''
static int esp32_mquickjs_wifi_raw_tx_broker_quiesce(unsigned generation,const void *pointer) {
    const esp32_mquickjs_wifi_raw_tx_token_t *token=pointer;
    assert(locks && !critical && !s_radio.stop_required && token->identity==raw_native.token.identity);
    assert(generation==raw_native.generation);
    if(!raw_unregistered){++raw_quiesces;if(raw_unregister_error)return raw_unregister_error;raw_unregistered=true;}
    return ESP_OK;
}
static bool esp32_mquickjs_wifi_raw_tx_broker_retire(esp32_mquickjs_wifi_raw_tx_token_t *token) {
    assert(locks && !critical && token->identity==raw_native.token.identity);
    /* The Radio must not call this boundary for ordinary completion while
     * recovery owns the lifecycle; only physical proof can retire it now. */
    assert(raw_native.native_terminated);memset(&raw_native,0,sizeof(raw_native));memset(token,0,sizeof(*token));return true;
}
'''

MAIN = r'''
int main(void) {
    for(unsigned temporary=0;temporary<2;++temporary) {
        reset_action();s_radio.stop_required=true;s_radio.event_live=WIFI_MODE_STA;
        s_channel_event_instance=&s_channel_callbacks;atomic_store(&s_channel_callbacks,0);
        memset(&s_raw_tx_recovery,0,sizeof(s_raw_tx_recovery));
        memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));
        memset(&s_config_restart,0,sizeof(s_config_restart));memset(&s_policy_restart,0,sizeof(s_policy_restart));
        stop_calls=stop_waits=deinit_calls=unregister_calls=0;raw_quiesces=0;raw_unregistered=false;
        stop_error=wait_error=deinit_error=unregister_error=raw_unregister_error=fence_error=0;wait_expired=false;
        esp32_mquickjs_wifi_radio_lease_t lease={0};
        wifi_radio_operation_lock();assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX,WIFI_MODE_STA,&lease));
        wifi_radio_live_lease_t *owner=wifi_radio_promiscuous_owner(lease.identity);
        /* Inject the completed submission boundary; production recovery must
         * preserve this original registry owner and broker identity. */
        owner->raw_tx_identity=37;owner->raw_tx_channel_pinned=true;owner->fixed_channel=true;
        wifi_radio_operation_unlock();
        raw_native=(esp32_mquickjs_wifi_raw_tx_broker_status_t){.generation=lease.generation,
            .token={lease.generation,37,lease.identity},.operation_active=true,.submit_returned=true};
        esp32_mquickjs_wifi_raw_tx_token_t operation=raw_native.token;
        esp32_mquickjs_wifi_radio_lifecycle_t token={0};wifi_mode_t mode;
        if(temporary) {
            s_tx_rate_lease.identity=lease.identity;s_tx_rate_lease.generation=lease.generation;
            s_tx_rate_lease.write_identity=10;s_tx_rate_lease.interface=WIFI_IF_STA;s_tx_rate_lease.previous=11;
            s_tx_rates.records[0]=(esp32_mquickjs_wifi_tx_rate_record_t){true,false,lease.generation,10};
        }
        esp32_mquickjs_wifi_raw_tx_token_t wrong=operation;++wrong.radio_lease_identity;
        assert(esp32_mquickjs_wifi_radio_begin_raw_tx_recovery(NULL,NULL,NULL,&wrong,&token,&mode)==ESP_ERR_INVALID_STATE);
        raw_native.submit_returned=false;
        assert(esp32_mquickjs_wifi_radio_begin_raw_tx_recovery(NULL,NULL,NULL,&operation,&token,&mode)==ESP_ERR_INVALID_STATE);
        raw_native.submit_returned=true;
        esp32_mquickjs_wifi_recovery_request_t selected={operation.generation,operation.identity,ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX};
        ++selected.identity;
        assert(esp32_mquickjs_wifi_radio_begin_recovery(NULL,NULL,NULL,&selected,&token,&mode)==ESP_ERR_INVALID_STATE);
        --selected.identity;selected.kind=ESP32_MQUICKJS_WIFI_RECOVERY_ACTION;
        assert(esp32_mquickjs_wifi_radio_begin_recovery(NULL,NULL,NULL,&selected,&token,&mode)==ESP_ERR_INVALID_STATE);
        selected.kind=ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX;
        assert(!esp32_mquickjs_wifi_radio_begin_recovery(NULL,NULL,NULL,&selected,&token,&mode));
        assert(!stop_calls && !raw_quiesces && !deinit_calls && mode==WIFI_MODE_STA);
        assert(!esp32_mquickjs_wifi_radio_raw_tx_retire(&lease,&operation) && owner->raw_tx_identity==37);
        esp32_mquickjs_wifi_radio_lifecycle_t stale=token;++stale.identity;
        assert(esp32_mquickjs_wifi_radio_stop_recovery(&stale)==ESP_ERR_INVALID_STATE && !stop_calls);
        stop_error=71;assert(esp32_mquickjs_wifi_radio_stop_recovery(&token)==71 && !s_raw_tx_recovery.stopped);
        stop_error=0;wait_error=72;
        assert(esp32_mquickjs_wifi_radio_stop_recovery(&token)==72 && stop_calls==2 && s_radio.stop_submitted);
        wait_error=0;assert(!esp32_mquickjs_wifi_radio_stop_recovery(&token) && stop_calls==2);
        assert(!esp32_mquickjs_wifi_radio_check_stopped_recovery(&token) && !raw_native.native_terminated);
        raw_unregister_error=73;
        assert(esp32_mquickjs_wifi_radio_shutdown_recovery(&token)==73 && !deinit_calls);
        raw_unregister_error=0;fence_error=74;
        assert(esp32_mquickjs_wifi_radio_shutdown_recovery(&token)==74 && !deinit_calls && !s_raw_tx_recovery.sdk_fenced);
        unsigned quiesced=raw_quiesces;fence_error=0;deinit_error=75;
        assert(esp32_mquickjs_wifi_radio_shutdown_recovery(&token)==75 && s_raw_tx_recovery.sdk_fenced);
        assert(raw_quiesces==quiesced && !raw_native.native_terminated && s_tx_rate_lease.identity==(temporary?lease.identity:0));
        deinit_error=0;unsigned generation=s_radio.generation;
        assert(esp32_mquickjs_wifi_radio_shutdown_recovery(&token)==ESP_ERR_TIMEOUT);
        assert(raw_native.native_terminated && !s_radio.driver_owned && s_radio.generation==generation && !s_tx_rate_lease.identity);
        assert(!esp32_mquickjs_wifi_radio_raw_tx_retire(&lease,&wrong));
        assert(esp32_mquickjs_wifi_radio_raw_tx_retire(&lease,&operation) && !owner->raw_tx_identity);
        assert(esp32_mquickjs_wifi_radio_shutdown_recovery(&token)==ESP_ERR_TIMEOUT && lease.acquired);
        wifi_radio_operation_lock();wifi_radio_release_locked(&lease);wifi_radio_operation_unlock();
        assert(!lease.acquired && !esp32_mquickjs_wifi_radio_shutdown_recovery(&token));
        assert(s_radio.generation==generation+1 && raw_quiesces==quiesced && deinit_calls==2);
        assert(!esp32_mquickjs_wifi_radio_finish_recovery(&token));
        assert(!esp32_mquickjs_wifi_radio_recovery_active(&token));
        assert(esp32_mquickjs_wifi_radio_stop_recovery(&token)==ESP_ERR_INVALID_STATE);
    }
    return 0;
}
'''
