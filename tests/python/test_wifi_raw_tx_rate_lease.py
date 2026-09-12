"""Deferred exact production Radio rate borrow/stop/restore/release tests.

Injected SDK start/stop, event wait, channel read and locks; no replacement rate,
lease, stop or rollback state machine. Physical callbacks/RF remain separate.
"""
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_wifi_tx_rate import COMPONENT, ROOT, rate_code
from test_wifi_config_controls import structure
from wireless_vm_fixture import extract
from test_wifi_rx_target import unit


def radio_rate_code(profile):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    raw = (COMPONENT / 'internal/esp32_mquickjs_wifi_raw_tx_validate.h').read_text()
    broker = (COMPONENT / 'internal/esp32_mquickjs_wifi_promiscuous_broker.h').read_text()
    symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][profile]['symbols']
    declarations = {key.split('::')[-1]: value['declaration'] for key, value in symbols.items()}
    code = rate_code(profile)
    for name in ('wifi_mode_t', 'wifi_second_chan_t'):
        code += declarations[name] + ';\n'
    code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_raw_tx_interface_t;', raw).group(0)
    for name in ('esp32_mquickjs_wifi_radio_client_t', 'esp32_mquickjs_wifi_radio_driver_state_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    code += structure(broker, 'esp32_mquickjs_wifi_promiscuous_token_t')
    code += structure(radio, 'wifi_radio_live_lease_t')
    for name in ('esp32_mquickjs_wifi_radio_lease_t', 'esp32_mquickjs_wifi_radio_promiscuous_lease_t'):
        code += structure(header, name)
    code += structure(header, 'esp32_mquickjs_wifi_radio_lifecycle_t')
    code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_action_lane.h')
    code += re.search(r'static struct \{\n    esp32_mquickjs_wifi_action_lane_t[^}]*\} s_action = [^;]*;', radio).group(0)
    code += r"""
static struct {bool stopped;} s_raw_tx_recovery;
static bool wifi_radio_raw_tx_recovery_exact_locked(const void *t) {(void)t;return false;}
static bool wifi_radio_raw_tx_recovery_owner_locked(const esp32_mquickjs_wifi_radio_lease_t *l) {(void)l;return false;}
"""
    code += ACTION_BOUNDARIES + BOUNDARIES
    for name in ('wifi_radio_lease_valid', 'wifi_radio_acquire_locked', 'wifi_radio_promiscuous_owner',
                 'wifi_radio_record_fault', 'wifi_radio_cleanup_fault', 'wifi_radio_release_locked',
                 'wifi_radio_stop_owners_locked', 'wifi_radio_stop_lease_locked', 'wifi_radio_stop_locked', 'wifi_radio_write_tx_rate',
                 'wifi_radio_restore_tx_rate_locked', 'esp32_mquickjs_wifi_radio_release_and_stop_idle',
                 'esp32_mquickjs_wifi_radio_raw_tx_acquire', 'esp32_mquickjs_wifi_radio_tx_rate_status'):
        code += extract(radio, name)
    return code


class WiFiRawTxRateLease(unittest.TestCase):
    def test_no_mutation_admission_known_restore_exact_owner_and_cleanup_suffixes(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                source.write_text(radio_rate_code(profile) + MAIN)
                built = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                        str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


BOUNDARIES = r'''
#define WIFI_RADIO_MAX_LEASES 4
#define WIFI_RADIO_NAN_PENDING false
#define WIFI_RADIO_MESH_PENDING false
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_TIMEOUT 5
#define ESP_ERR_WIFI_NOT_STARTED 6
#define RADIO_EVENTS_IDLE 0
#define RADIO_EVENTS_STOP 2
#define AP_STOP_IDLE 0
static unsigned locks,critical,writes,starts,stops,waits;
static bool stop_observed;
static int next_write[2],stop_error,wait_error,start_error;
static wifi_tx_rate_config_t native_rate={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M};
static struct {
    int lock;
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];
    unsigned generation,next_lease_identity,clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT];
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    bool driver_owned,storage_configured,started,stop_required,stop_submitted,restart_required,promiscuous_claimed;
    unsigned wake_locks;
    struct {uint32_t identity,lease_identity;} operation,lifecycle;
    const char *fault_stage,*cleanup_stage;esp_err_t fault_error,cleanup_error;
    wifi_mode_t effective_mode,event_live;
    unsigned event_phase,ap_stop_phase,ap_transition_application,ap_transition_station,ap_transition_access_point;
    bool ap_reopen_pending,ap_reopen_attempted,ap_reopen_quiesced,ap_reopen_restore_complete;
    esp_err_t ap_stop_error;
} s_radio={.generation=7,.next_lease_identity=1,.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED,
    .driver_owned=true,.storage_configured=true};
/* Unrelated connectionless owner is an injected admission boundary. */
static struct {
    struct {uint32_t identity,owner_identity,generation;} owner;
    bool uncertain,restore_pending;
} s_interval;
static esp32_mquickjs_wifi_tx_rate_state_t s_tx_rates={.next_identity=2};
static esp32_mquickjs_wifi_tx_rate_lease_t s_tx_rate_lease;
static const char s_tx_rate_restore_fault[]="tx-rate-restore";
static void wifi_radio_operation_lock(void) {assert(!locks);++locks;}
static void wifi_radio_operation_unlock(void) {assert(locks==1);--locks;}
#define taskENTER_CRITICAL(lock) do {(void)(lock);assert(!critical);++critical;} while(0)
#define taskEXIT_CRITICAL(lock) do {(void)(lock);assert(critical==1);--critical;} while(0)
static void wifi_radio_set_state(esp32_mquickjs_wifi_radio_driver_state_t state) {s_radio.driver_state=state;}
static void wifi_radio_release_promiscuous_locked(esp32_mquickjs_wifi_radio_promiscuous_lease_t *lease) {(void)lease;assert(0);}
static int wifi_radio_begin_events(unsigned phase,wifi_mode_t mode) {(void)mode;assert(locks && !critical);s_radio.event_phase=phase;stop_observed=false;return ESP_OK;}
/* Getter capture has its own production fixture. This boundary verifies that
 * the actual stop path observes before its first SDK submission. */
static void wifi_radio_capture_stop_snapshot_locked(void) {assert(locks && !critical && !stop_observed);stop_observed=true;}
static int wifi_radio_wait_events(void) {assert(locks && !critical);++waits;if(wait_error)return wait_error;s_radio.event_phase=RADIO_EVENTS_IDLE;return ESP_OK;}
static int esp_wifi_stop(void) {assert(locks && !critical && s_tx_rate_lease.identity && stop_observed);++stops;return stop_error;}
static int wifi_radio_validate_regulatory_channel(uint8_t channel) {assert(channel==6);return ESP_OK;}
static uint32_t esp32_mquickjs_wifi_radio_5ghz_channel_bit(uint8_t channel) {return channel==36?1:0;}
static int wifi_radio_get_channel_locked(uint8_t *channel,wifi_second_chan_t *secondary,uint32_t *revision) {*channel=6;*secondary=WIFI_SECOND_CHAN_NONE;*revision=1;return ESP_OK;}
static int wifi_radio_set_channel_locked(esp32_mquickjs_wifi_radio_lease_t *lease,uint8_t channel,wifi_second_chan_t secondary) {(void)secondary;assert(lease->acquired && channel==6);return ESP_OK;}
static int wifi_radio_ensure_started_locked(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(locks && !critical && s_tx_rate_lease.identity==lease->identity && writes);
    ++starts;s_radio.stop_required=true;
    if(start_error){s_radio.fault_stage="start";s_radio.fault_error=start_error;return start_error;}
    s_radio.started=true;s_radio.effective_mode=WIFI_MODE_STA;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    return ESP_OK;
}
static esp_err_t esp_wifi_config_80211_tx(wifi_interface_t interface,wifi_tx_rate_config_t *config) {
    assert(locks && !critical && !s_radio.started && !s_radio.stop_required && interface==WIFI_IF_STA && s_tx_rate_lease.identity);
    ++writes;int error=next_write[0];next_write[0]=next_write[1];next_write[1]=0;
    native_rate=*config; /* Even a failure may have changed driver state. */
    return error;
}
'''

MAIN = r'''
#define radio(name) esp32_mquickjs_wifi_radio_##name
static wifi_tx_rate_config_t requested={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_9M};
static uint8_t channel;
static int open_rate(esp32_mquickjs_wifi_radio_lease_t *lease) {
    return radio(raw_tx_acquire)(ESP32_MQUICKJS_WIFI_RAW_TX_STATION,6,&requested,lease,&channel);
}
static void baseline(void) {
    s_tx_rates.records[0]=(esp32_mquickjs_wifi_tx_rate_record_t){.known=true,.generation=7,.write_identity=1,
        .config={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M}};
}
int main(void) {
    (void)esp32_mquickjs_wifi_radio_5ghz_channel_bit;
    esp32_mquickjs_wifi_radio_lease_t lease={0},other={0};
    assert(open_rate(&lease)==ESP_ERR_INVALID_STATE && !lease.acquired && !writes && !starts);
    baseline();s_radio.started=true;assert(open_rate(&lease)==ESP_ERR_INVALID_STATE && !writes);s_radio.started=false;
    s_radio.wake_locks=1;assert(open_rate(&lease)==ESP_ERR_INVALID_STATE && !writes);s_radio.wake_locks=0;
    s_radio.leases[0].identity=99;assert(open_rate(&lease)==ESP_ERR_INVALID_STATE && !writes);s_radio.leases[0].identity=0;
    assert(radio(raw_tx_acquire)(ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT,6,&requested,&lease,&channel)==ESP_ERR_NOT_SUPPORTED && !writes);
    uint32_t next=s_tx_rates.next_identity;s_tx_rates.next_identity=UINT32_MAX;
    assert(open_rate(&lease)==ESP_ERR_INVALID_STATE && !writes);s_tx_rates.next_identity=next;
    /* Production transaction admission runs before allocating a Radio owner. */
    for(unsigned bad=0;bad<4;++bad) {
        baseline();
        switch(bad) {
        case 0:s_tx_rates.records[0].uncertain=true;break;
        case 1:s_tx_rates.records[0].write_identity=0;break;
        case 2:s_tx_rates.records[0].config.rate=(wifi_phy_rate_t)9999;break;
        case 3:s_tx_rate_lease.restore_pending=true;break;
        }
        uint32_t identity_before=s_radio.next_lease_identity;
        assert(open_rate(&lease)==ESP_ERR_INVALID_STATE && !lease.acquired && !writes && !starts);
        assert(s_radio.next_lease_identity==identity_before);
        s_tx_rate_lease.restore_pending=false;
    }
    baseline();
    assert(open_rate(&lease)==ESP_OK && lease.acquired && native_rate.rate==WIFI_PHY_RATE_9M);
    assert(s_tx_rate_lease.identity==lease.identity && starts==1 && writes==1);
    esp32_mquickjs_wifi_radio_lease_t stale=lease;stale.generation++;
    assert(radio(release_and_stop_idle)(&stale)==ESP_ERR_INVALID_STATE && stale.acquired && !stops);
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&other)==ESP_ERR_INVALID_STATE);
    assert(wifi_radio_stop_locked()==ESP_ERR_INVALID_STATE && !stops);
    wifi_radio_release_locked(&lease);assert(lease.acquired);
    wifi_radio_operation_unlock();
    /* A pending native packet excludes stop/restore, preserving owner/grant. */
    s_radio.leases[0].raw_tx_identity=88;
    assert(radio(release_and_stop_idle)(&lease)==ESP_ERR_INVALID_STATE && !stops && lease.acquired);
    s_radio.leases[0].raw_tx_identity=0;
    wait_error=51;assert(radio(release_and_stop_idle)(&lease)==51 && stops==1 && writes==1 && lease.acquired);
    wait_error=0;next_write[0]=52;next_write[1]=0;
    assert(radio(release_and_stop_idle)(&lease)==52 && stops==1 && writes==3 && lease.acquired);
    assert(native_rate.rate==WIFI_PHY_RATE_9M && s_tx_rate_lease.restore_pending && s_tx_rate_lease.restore_error==52);
    esp32_mquickjs_wifi_tx_rate_record_t record;esp32_mquickjs_wifi_tx_rate_lease_t temporary;uint32_t generation;
    assert(radio(tx_rate_status)(WIFI_IF_STA,&record,&generation,&temporary)==ESP_OK);
    assert(temporary.identity==lease.identity && temporary.previous.rate==WIFI_PHY_RATE_6M && record.known);
    assert(radio(release_and_stop_idle)(&lease)==ESP_OK && !lease.acquired && !s_tx_rate_lease.identity && writes==4 && stops==1);
    assert(native_rate.rate==WIFI_PHY_RATE_6M && !s_radio.fault_stage && !s_radio.cleanup_stage);
    assert(radio(release_and_stop_idle)(&lease)==ESP_OK && writes==4 && stops==1);
    /* Failed initial write + failed rollback retains the exact predecessor. */
    next_write[0]=61;next_write[1]=62;
    assert(open_rate(&lease)==61 && lease.acquired && s_tx_rate_lease.restore_pending && starts==1);
    assert(radio(release_and_stop_idle)(&lease)==ESP_OK && !lease.acquired && native_rate.rate==WIFI_PHY_RATE_6M && stops==1);
    /* A successful borrow followed by failed start restores, preserving its
     * unrelated lifecycle diagnostic. The rate adapter does not reset driver. */
    start_error=71;assert(open_rate(&lease)==71 && lease.acquired);
    assert(radio(release_and_stop_idle)(&lease)==ESP_OK && !lease.acquired && !s_tx_rate_lease.identity);
    assert(native_rate.rate==WIFI_PHY_RATE_6M && s_radio.fault_error==71 && !strcmp(s_radio.fault_stage,"start"));
    assert(!locks && !critical);
    return 0;
}
'''

# This fixture exercises only the temporary-rate exception. The Action recovery
# exception is covered with the real registry/ledger in test_wifi_action_recovery.
ACTION_BOUNDARIES = r'''
static bool wifi_radio_action_recovery_exact_locked(const void *token) {(void)token;assert(!"unexpected Action recovery");return false;}
static bool wifi_radio_action_exact_locked(const esp32_mquickjs_wifi_action_token_t *token) {(void)token;assert(!"unexpected Action owner");return false;}
'''
