"""Deferred production Radio admission/retirement plus the actual Raw TX broker.

Uses pinned SDK types, injected SDK calls and pthread scheduling. The Radio
registry, lease checks, channel observation and send/retire/release are production
functions. AP startup and physical shutdown are outside this fixture's scope.
"""
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_wifi_raw_tx_broker import production_code
from test_wifi_config_controls import ROOT, RADIO, HEADER, structure
from wireless_vm_fixture import extract


def radio_code(profile):
    code = production_code(profile)
    code = code.replace('static esp_err_t esp_wifi_80211_tx(',
                        'static void radio_send_hook(void);\nstatic esp_err_t esp_wifi_80211_tx(')
    code = code.replace('++sends;driver_bytes=bytes;', '++sends;driver_bytes=bytes;radio_send_hook();')
    # The baseline broker fixture asserts STA; this integration also sends AP.
    code = code.replace('interface==WIFI_IF_STA && length==24',
                        '(interface==WIFI_IF_STA || interface==WIFI_IF_AP) && length==24')
    symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][profile]['symbols']
    declarations = {}
    for key, entry in symbols.items():
        value = entry.get('declaration', '')
        if value.startswith('typedef '):
            name = key.split('::')[-1]
            if name not in declarations or '{' in value:
                declarations[name] = value
    seen = {'wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t', 'wifi_tx_info_t', 'esp_80211_tx_info_t'}
    extra = []

    def visit(name):
        if name in seen:
            return
        seen.add(name)
        for dependency in sorted(set(re.findall(r'\b\w+_t\b', declarations[name])) - {name}):
            if dependency in declarations:
                visit(dependency)
        extra.append(declarations[name] + ';\n')

    for name in ['wifi_mode_t', 'wifi_second_chan_t', 'wifi_ap_record_t', 'wifi_sta_list_t']:
        visit(name)
    code += ''.join(extra)
    header, radio = HEADER.read_text(), RADIO.read_text()
    code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;', header).group(0)
    broker_header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_promiscuous_broker.h').read_text()
    code += structure(broker_header, 'esp32_mquickjs_wifi_promiscuous_token_t')
    code += structure(radio, 'wifi_radio_live_lease_t')
    for name in ['esp32_mquickjs_wifi_radio_lease_t', 'esp32_mquickjs_wifi_radio_promiscuous_lease_t']:
        code += structure(header, name)
    code += r"""
static struct {esp32_mquickjs_wifi_raw_tx_token_t operation;} s_raw_tx_recovery;
static bool wifi_radio_raw_tx_recovery_exact_locked(const void *token) {(void)token;return false;}
"""
    interval = (ROOT / "components/esp32_mquickjs/internal/esp32_mquickjs_wifi_interval.h").read_text()
    for name in ["esp32_mquickjs_wifi_interval_token_t", "esp32_mquickjs_wifi_interval_state_t"]:
        code += structure(interval, name)
    code += BOUNDARIES
    for name in ['wifi_radio_lease_valid', 'wifi_radio_acquire_locked', 'wifi_radio_promiscuous_owner',
                 'wifi_radio_refresh_channel', 'wifi_radio_get_channel_locked', 'wifi_radio_release_channel_locked',
                 'wifi_radio_release_locked', 'esp32_mquickjs_wifi_radio_release',
                 'wifi_radio_raw_tx_policy', 'wifi_radio_raw_tx_unpin',
                 'esp32_mquickjs_wifi_radio_raw_tx_submit', 'esp32_mquickjs_wifi_radio_raw_tx_retire']:
        code += extract(radio, name)
    return code + MAIN


class WiFiRawTxRadio(unittest.TestCase):
    def test_exact_lease_channel_pinning_association_and_concurrent_release(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                source.write_text(radio_code(profile))
                built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                        str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


BOUNDARIES = r'''
#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1
#define WIFI_RADIO_MAX_LEASES 4U
#define WIFI_RADIO_NAN_PENDING false
#define WIFI_RADIO_MESH_PENDING false
static esp32_mquickjs_wifi_interval_state_t s_interval;
#define ESP_ERR_WIFI_NOT_CONNECT 101
#define ESP_ERR_NOT_SUPPORTED 102
#define taskENTER_CRITICAL portENTER_CRITICAL
#define taskEXIT_CRITICAL portEXIT_CRITICAL
static struct { uint32_t identity,generation; } s_tx_rate_lease;
static struct {
    portMUX_TYPE lock;
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];
    uint32_t clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT],generation,next_lease_identity;
    bool started;wifi_mode_t effective_mode;
    const char *fault_stage;esp_err_t fault_error;
    struct { uint32_t identity,lease_identity; } lifecycle,operation;
    uint8_t primary_channel;wifi_second_chan_t secondary_channel;
    uint32_t channel_generation;uint64_t channel_observation_revision;
    esp_err_t channel_observation_error;
} s_radio={.lock=portMUX_INITIALIZER_UNLOCKED,.generation=7,.next_lease_identity=123,
    .started=true,.effective_mode=WIFI_MODE_STA,.primary_channel=6};
static pthread_mutex_t operation_lock=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local unsigned operation_depth;
static _Thread_local bool release_task;
static bool try_release,release_entered;
static pthread_t release_worker;
static esp32_mquickjs_wifi_radio_lease_t *releasing_lease;
static void wifi_radio_operation_lock(void) {
    assert(!critical_depth && !operation_depth);
    if(release_task) {
        assert(!pthread_mutex_lock(&pause_lock));release_entered=true;
        assert(!pthread_cond_broadcast(&changed));assert(!pthread_mutex_unlock(&pause_lock));
    }
    assert(!pthread_mutex_lock(&operation_lock));++operation_depth;
}
static void wifi_radio_operation_unlock(void) {
    assert(operation_depth==1 && !critical_depth);--operation_depth;
    assert(!pthread_mutex_unlock(&operation_lock));
}
static void wifi_radio_release_promiscuous_locked(esp32_mquickjs_wifi_radio_promiscuous_lease_t *lease) {
    (void)lease;assert(!"Raw TX must never own or release a promiscuous lease");
}
static wifi_ap_record_t connected_ap;
static wifi_sta_list_t connected_clients;
static bool station_connected;
static esp_err_t mode_error,association_error,mac_error,channel_error;
static wifi_mode_t driver_mode=WIFI_MODE_STA;
static uint8_t driver_channel=6;
static esp_err_t esp_wifi_get_mode(wifi_mode_t *mode) {
    assert(operation_depth==1 && !critical_depth);*mode=driver_mode;return mode_error;
}
static esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) {
    assert(operation_depth==1 && !critical_depth);*ap=connected_ap;
    return association_error ? association_error : station_connected ? ESP_OK : ESP_ERR_WIFI_NOT_CONNECT;
}
static esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *clients) {
    assert(operation_depth==1 && !critical_depth);*clients=connected_clients;return association_error;
}
static esp_err_t esp_wifi_get_mac(wifi_interface_t interface,uint8_t *mac) {
    assert(operation_depth==1 && !critical_depth && (interface==WIFI_IF_STA || interface==WIFI_IF_AP));
    memcpy(mac,source_address,6);return mac_error;
}
static esp_err_t esp_wifi_get_channel(uint8_t *primary,wifi_second_chan_t *secondary) {
    assert(operation_depth==1 && !critical_depth);*primary=driver_channel;*secondary=WIFI_SECOND_CHAN_NONE;
    return channel_error;
}
'''

MAIN = r'''
#define radio(name) esp32_mquickjs_wifi_radio_raw_tx_##name
#define broker(name) esp32_mquickjs_wifi_raw_tx_broker_##name
static uint8_t bytes[24]={0x80};
static uint8_t actual;
static esp32_mquickjs_wifi_raw_tx_validation_t validation;
static void *release_thread(void *unused) {
    (void)unused;release_task=true;esp32_mquickjs_wifi_radio_release(releasing_lease);return NULL;
}
static void radio_send_hook(void) {
    assert(operation_depth==1 && !critical_depth);
    if(try_release) {
        try_release=false;release_entered=false;
        assert(!pthread_create(&release_worker,NULL,release_thread,NULL));
        assert(!pthread_mutex_lock(&pause_lock));
        while(!release_entered)assert(!pthread_cond_wait(&changed,&pause_lock));
        assert(!pthread_mutex_unlock(&pause_lock));
        assert(releasing_lease->acquired && s_radio.leases[0].fixed_channel);
    }
}
static esp_err_t send_frame(esp32_mquickjs_wifi_radio_lease_t *lease,token_t *token,bool sequence) {
    sending_token=token;
    esp_err_t result=radio(submit)(lease,driver_mode==WIFI_MODE_AP ? ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT :
        ESP32_MQUICKJS_WIFI_RAW_TX_STATION,sequence,bytes,24,token,&validation,&actual);
    sending_token=NULL;return result;
}
static void acquire(esp32_mquickjs_wifi_radio_lease_t *lease) {
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX,driver_mode,lease)==ESP_OK);
    wifi_radio_operation_unlock();
}
int main(void) {
    memset(bytes+4,0x34,6);memset(bytes+10,0x12,6);
    memcpy(destination,bytes+4,6);memcpy(source_address,bytes+10,6);memcpy(connected_ap.bssid,destination,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS};
    esp32_mquickjs_wifi_radio_lease_t lease={0};token_t token={0};acquire(&lease);
    assert(lease.identity==123 && s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX]==1);
    esp32_mquickjs_wifi_radio_lease_t stale=lease;++stale.identity;
    assert(send_frame(&stale,&token,true)==ESP_ERR_INVALID_STATE && !sends && !registrations);
    s_radio.operation.identity=1;
    assert(send_frame(&lease,&token,true)==ESP_ERR_INVALID_STATE && !sends && !registrations);
    s_radio.operation.identity=0;
    bytes[0]=0x88;
    assert(send_frame(&lease,&token,true)==ESP_ERR_INVALID_ARG && !sends && !registrations);bytes[0]=0x80;
    station_connected=true;
    assert(send_frame(&lease,&token,false)==ESP_ERR_INVALID_ARG && !sends && !registrations);
    bytes[1]=0x08; /* Retry on the actual connected management path. */
    assert(send_frame(&lease,&token,true)==ESP_ERR_INVALID_ARG && !sends && !registrations);bytes[1]=0;
    mac_error=72;assert(send_frame(&lease,&token,true)==72 && !sends && !registrations);mac_error=0;
    station_connected=false;channel_error=73;
    assert(send_frame(&lease,&token,true)==73 && !sends && !registrations);channel_error=0;
    fail_alloc=true;
    assert(send_frame(&lease,&token,true)==ESP_ERR_NO_MEM && !token.identity);
    assert(!s_radio.leases[0].fixed_channel && !s_radio.leases[0].raw_tx_identity);fail_alloc=false;
    releasing_lease=&lease;try_release=true;
    assert(send_frame(&lease,&token,true)==ESP_OK);
    assert(!pthread_join(release_worker,NULL));
    assert(lease.acquired && actual==6 && s_radio.leases[0].raw_tx_identity==token.identity);
    assert(s_radio.leases[0].fixed_channel && s_radio.leases[0].raw_tx_channel_pinned);
    wifi_radio_operation_lock();wifi_radio_release_channel_locked(&lease);wifi_radio_operation_unlock();
    assert(s_radio.leases[0].fixed_channel);
    assert(broker(abandon)(&token));
    assert(!radio(retire)(&lease,&token));
    esp32_mquickjs_wifi_radio_release(&lease);assert(lease.acquired && allocations==frees+1);
    driver_callback(&callback_info);
    assert(!radio(retire)(&stale,&token) && token.identity);
    assert(radio(retire)(&lease,&token) && !token.identity && allocations==frees);
    assert(!s_radio.leases[0].fixed_channel && !s_radio.leases[0].raw_tx_identity);
    assert(!radio(retire)(&lease,&token));
    /* A preexisting numeric channel claim survives successful retirement. */
    s_radio.leases[0].fixed_channel=true;s_radio.leases[0].primary_channel=6;
    synchronous=true;assert(send_frame(&lease,&token,true)==ESP_OK);
    assert(radio(retire)(&lease,&token) && s_radio.leases[0].fixed_channel);
    driver_channel=11;
    unsigned calls=sends;
    assert(send_frame(&lease,&token,true)==ESP_ERR_INVALID_STATE && sends==calls && !token.identity);
    assert(s_radio.leases[0].channel_conflict);
    esp32_mquickjs_wifi_radio_release(&lease);assert(!lease.acquired && !s_radio.leases[0].identity);
    /* AP sharing admits Raw TX only on an already active required interface. */
    driver_channel=6;driver_mode=s_radio.effective_mode=WIFI_MODE_AP;
    s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP]=1;
    acquire(&lease);callback_info.ifidx=WIFI_IF_AP;
    connected_clients.num=1;memcpy(connected_clients.sta[0].mac,destination,6);
    assert(send_frame(&lease,&token,false)==ESP_ERR_INVALID_ARG && sends==calls);
    bytes[0]=0x08;bytes[1]=0x01; /* AP must use FromDS on this actual path. */
    assert(send_frame(&lease,&token,true)==ESP_ERR_INVALID_ARG && sends==calls);
    bytes[1]=0x02;
    assert(send_frame(&lease,&token,true)==ESP_OK && radio(retire)(&lease,&token));
    esp32_mquickjs_wifi_radio_release(&lease);assert(!lease.acquired);
    assert(s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP]==1);
    assert(!s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX] && allocations==frees);
    /* Exercise the shared callback task boundary as well as synchronous sends. */
    start_callback();finish_callback();
    assert(broker(unregister)(7)==ESP_OK && broker(reset_after_deinit)(7));
    assert(!operation_depth && !critical_depth);
    return 0;
}
'''
