"""Deferred tests of the production TX broker, validator and callback snapshot.

Only SDK, clock, allocator and task locks are substituted. No fixture state
machine stands in for the broker. Run with the Wi-Fi stage, not during API work.
"""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_wifi_rx_target import ROOT, INTERNAL, COMMON, unit

RAW = ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx'


def production_code(profile):
    inventory = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants']
    symbols = {key.split('::')[-1]: value['declaration']
               for key, value in inventory[profile]['symbols'].items()}
    code = PRELUDE
    for name in ['wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t',
                 'wifi_tx_info_t', 'esp_80211_tx_info_t']:
        code += symbols[name] + ';\n'
    for name in ['wifi_rx', 'wifi_raw_tx_validate', 'wifi_raw_tx_snapshot', 'wifi_raw_tx_broker']:
        code += unit(INTERNAL / ('esp32_mquickjs_' + name + '.h'))
    code += BOUNDARIES
    code += unit(COMMON / 'esp32_mquickjs_wifi_rx.c')
    for name in ['validate', 'snapshot', 'broker']:
        if name == 'broker': code += '#define esp32_mquickjs_memory_payload_free heap_caps_free\n'
        code += unit(RAW / ('esp32_mquickjs_wifi_raw_tx_' + name + '.c'))
        if name == 'broker': code += '#undef esp32_mquickjs_memory_payload_free\n'
    return code


class WiFiRawTxBroker(unittest.TestCase):
    def test_native_completion_storage_quarantine_and_registration_fence(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                source.write_text(production_code(profile) + MAIN)
                built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                        str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


PRELUDE = r'''
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define MALLOC_CAP_8BIT 1
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
static _Thread_local unsigned critical_depth;
#define portENTER_CRITICAL(lock) do { assert(!pthread_mutex_lock(lock)); ++critical_depth; } while(0)
#define portEXIT_CRITICAL(lock) do { --critical_depth; assert(!pthread_mutex_unlock(lock)); } while(0)
'''

BOUNDARIES = r'''
#define esp32_mquickjs_memory_wireless_alloc(owner,size,kind,role) heap_caps_malloc(size,MALLOC_CAP_8BIT)
#define esp32_mquickjs_memory_wireless_calloc(owner,count,size,kind,role) heap_caps_calloc(count,size,MALLOC_CAP_8BIT)
typedef esp32_mquickjs_wifi_raw_tx_token_t token_t;
typedef esp32_mquickjs_wifi_raw_tx_broker_status_t status_t;
typedef esp32_mquickjs_wifi_raw_tx_validation_policy_t policy_t;
static void (*driver_callback)(const esp_80211_tx_info_t *);
static unsigned allocations, frees, sends, registrations, unregisters;
static bool fail_alloc, synchronous, start_on_unregister;
static esp_err_t register_error, unregister_error, send_error;
static const uint8_t *driver_bytes;
static token_t *sending_token;
static esp_80211_tx_info_t callback_info;
static uint8_t destination[6], source_address[6];
static pthread_mutex_t pause_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool paused, proceed;
static _Thread_local bool pause_clock;
static pthread_t callback_task;
static void *heap_caps_malloc(size_t size, unsigned caps) {
    assert(!critical_depth && caps==MALLOC_CAP_8BIT);
    if(fail_alloc)return NULL;
    void *p=malloc(size);assert(p);++allocations;return p;
}
static void heap_caps_free(void *p) {
    assert(!critical_depth && p && allocations>frees);++frees;free(p);
}
static int64_t esp_timer_get_time(void) {
    assert(!critical_depth);
    if(pause_clock) {
        assert(!pthread_mutex_lock(&pause_lock));paused=true;
        assert(!pthread_cond_broadcast(&changed));
        while(!proceed)assert(!pthread_cond_wait(&changed,&pause_lock));
        assert(!pthread_mutex_unlock(&pause_lock));
    }
    return 123456789;
}
static void *callback_thread(void *unused) {
    (void)unused;pause_clock=true;driver_callback(&callback_info);return NULL;
}
static void start_callback(void) {
    paused=proceed=false;
    assert(!pthread_create(&callback_task,NULL,callback_thread,NULL));
    assert(!pthread_mutex_lock(&pause_lock));
    while(!paused)assert(!pthread_cond_wait(&changed,&pause_lock));
    assert(!pthread_mutex_unlock(&pause_lock));
}
static void finish_callback(void) {
    assert(!pthread_mutex_lock(&pause_lock));proceed=true;
    assert(!pthread_cond_broadcast(&changed));assert(!pthread_mutex_unlock(&pause_lock));
    assert(!pthread_join(callback_task,NULL));
}
static esp_err_t esp_wifi_register_80211_tx_cb(void (*callback)(const esp_80211_tx_info_t *)) {
    assert(!critical_depth);
    if(callback) {++registrations;driver_callback=callback;return register_error;}
    ++unregisters;
    /* A callback already entered may outlive successful unregister. */
    if(start_on_unregister) {start_on_unregister=false;start_callback();}
    return unregister_error;
}
static esp_err_t esp_wifi_80211_tx(wifi_interface_t interface,const void *bytes,int length,bool sequence) {
    assert(!critical_depth && interface==WIFI_IF_STA && length==24 && sequence);
    ++sends;driver_bytes=bytes;
    assert(sending_token && sending_token->identity);
    status_t status;esp32_mquickjs_wifi_raw_tx_broker_status(&status);
    assert(status.operation_active && !status.submit_returned && !status.driver_accepted);
    assert(!esp32_mquickjs_wifi_raw_tx_broker_retire(sending_token));
    assert(!esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(sending_token->generation));
    if(synchronous) {
        driver_callback(&callback_info);
        esp32_mquickjs_wifi_raw_tx_broker_status(&status);
        assert(status.driver_completed && !status.submit_returned);
        assert(!esp32_mquickjs_wifi_raw_tx_broker_retire(sending_token));
    }
    return send_error;
}
'''

MAIN = r'''
#define broker(name) esp32_mquickjs_wifi_raw_tx_broker_##name
static uint8_t bytes[24]={0x80};
static policy_t policy={.interface=ESP32_MQUICKJS_WIFI_RAW_TX_STATION,.driver_sequence=true};
static esp_err_t submit(token_t *token) {
    sending_token=token;esp32_mquickjs_wifi_raw_tx_validation_t validation;
    esp_err_t error=broker(submit)(7,123,bytes,sizeof(bytes),&policy,token,&validation);
    assert(validation==ESP32_MQUICKJS_WIFI_RAW_TX_VALID);sending_token=NULL;return error;
}
static status_t status(void) {status_t result;broker(status)(&result);return result;}
static void retire(token_t *token) {
    token_t stale=*token;unsigned before=frees;
    assert(broker(retire)(token) && !token->identity && frees==before+1);
    assert(!broker(retire)(&stale) && !broker(abandon)(&stale));
    assert(allocations==frees && !status().operation_active);
}
static void consume_termination(token_t *token) {
    unsigned freed=frees;token_t stale=*token,wrong=*token;wrong.identity++;
    assert(status().native_terminated && !status().operation_active && status().token.identity==token->identity);
    assert(broker(register)(7)==ESP_ERR_INVALID_STATE && broker(register)(8)==ESP_ERR_INVALID_STATE);
    assert(!broker(retire)(&wrong));
    assert(broker(reset_after_deinit)(token->generation) && status().native_terminated && frees==freed);
    assert(broker(retire)(token) && !token->identity && frees==freed && !status().native_terminated);
    assert(!broker(retire)(&stale) && !broker(abandon)(&stale));
}
int main(void) {
    memset(bytes+4,0x34,6);memset(bytes+10,0x12,6);
    memcpy(destination,bytes+4,6);memcpy(source_address,bytes+10,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS,.data=(uint8_t *)(uintptr_t)1,.data_len=255};
    token_t token={0},next={0};
    assert(broker(register)(0)==ESP_ERR_INVALID_ARG);
    register_error=77;
    assert(broker(register)(7)==77 && status().registration_uncertain);
    assert(broker(register)(7)==ESP_ERR_INVALID_STATE && registrations==1);
    assert(submit(&token)==ESP_ERR_INVALID_STATE && !token.identity && allocations==frees);
    unregister_error=78;
    assert(broker(unregister)(7)==78 && status().generation==7);
    unregister_error=0;
    assert(broker(unregister)(7)==ESP_OK && status().unregister_written);
    unsigned calls=unregisters;
    assert(broker(unregister)(7)==ESP_OK && unregisters==calls);
    assert(broker(register)(7)==ESP_ERR_INVALID_STATE);
    assert(broker(register)(8)==ESP_ERR_INVALID_STATE);
    assert(!broker(reset_after_deinit)(8) && broker(reset_after_deinit)(7));
    register_error=0;
    assert(broker(register)(7)==ESP_OK);
    calls=registrations;assert(broker(register)(7)==ESP_OK && registrations==calls);
    esp32_mquickjs_wifi_raw_tx_validation_t validation;
    calls=sends;unsigned allocated=allocations;
    assert(broker(submit)(7,123,bytes,23,&policy,&token,&validation)==ESP_ERR_INVALID_ARG);
    assert(sends==calls && allocations==allocated && !token.identity);
    fail_alloc=true;assert(submit(&token)==ESP_ERR_NO_MEM && !token.identity);fail_alloc=false;
    synchronous=true;
    assert(submit(&token)==ESP_OK && status().driver_completed && status().driver_accepted);
    assert(driver_bytes!=bytes && !memcmp(driver_bytes,bytes,24));
    assert(status().completion.raw_body_length==255 && status().completion.source[0]==0x12);
    token_t stale=token;retire(&token);
    synchronous=false;assert(submit(&token)==ESP_OK && token.identity!=stale.identity);
    assert(!broker(abandon)(&stale) && !broker(retire)(&stale));
    token_t wrong=token;++wrong.radio_lease_identity;assert(!broker(abandon)(&wrong));
    assert(broker(abandon)(&token) && status().quarantined);
    assert(submit(&next)==ESP_ERR_INVALID_STATE && !next.identity);
    assert(broker(unregister)(7)==ESP_ERR_INVALID_STATE && !broker(retire)(&token));
    /* Public timeout/close keeps the driver's private copy intact. */
    bytes[23]=99;assert(driver_bytes[23]==0);bytes[23]=0;
    driver_callback(NULL);assert(status().invalid_callbacks==1 && !status().driver_completed);
    destination[0]^=1;driver_callback(&callback_info);destination[0]^=1;
    callback_info.ifidx=WIFI_IF_AP;driver_callback(&callback_info);callback_info.ifidx=WIFI_IF_STA;
    assert(status().mismatched_callbacks==2 && !status().driver_completed);
    start_callback();
    assert(status().callbacks_active==1 && !broker(retire)(&token));
    assert(!broker(reset_after_deinit)(7) && broker(unregister)(7)==ESP_ERR_INVALID_STATE);
    assert(allocations==frees+1 && !memcmp(driver_bytes,bytes,24));
    finish_callback();assert(status().driver_completed && !status().quarantined);
    assert(status().abandoned);retire(&token);
    /* Submission failure cannot prove that native storage is unreferenced. */
    send_error=81;assert(submit(&token)==81 && token.identity);
    assert(status().quarantined && !status().driver_accepted && status().submit_error==81);
    assert(!broker(retire)(&token) && broker(unregister)(7)==ESP_ERR_INVALID_STATE);
    driver_callback(&callback_info);
    assert(status().correlation_fault && !broker(retire)(&token));
    token_t wrong_recovery=token;++wrong_recovery.radio_lease_identity;
    assert(broker(quiesce)(7,&wrong_recovery)==ESP_ERR_INVALID_STATE && broker(quiesce)(7,NULL)==ESP_ERR_INVALID_ARG);
    calls=unregisters;unregister_error=82;
    assert(broker(quiesce)(7,&token)==82 && status().operation_active && !status().native_terminated && allocations==frees+1);
    unregister_error=0;assert(broker(quiesce)(7,&token)==ESP_OK && unregisters==calls+2);
    assert(broker(quiesce)(7,&token)==ESP_OK && unregisters==calls+2 && !broker(retire)(&token));
    assert(status().submit_error==81 && status().correlation_fault);
    assert(broker(reset_after_deinit)(7) && allocations==frees);
    assert(status().native_terminated && status().submit_error==81 && status().correlation_fault);
    consume_termination(&token);
    send_error=0;assert(broker(register)(7)==ESP_OK && submit(&token)==ESP_OK);
    driver_callback(&callback_info);driver_callback(&callback_info);
    assert(status().duplicate_callbacks==1 && status().quarantined && !broker(retire)(&token));
    assert(broker(reset_after_deinit)(7));consume_termination(&token);
    assert(broker(register)(7)==ESP_OK);
    /* Successful unregister retries only the callback drain suffix. */
    start_on_unregister=true;calls=unregisters;
    assert(broker(unregister)(7)==ESP_ERR_TIMEOUT && status().unregister_written);
    assert(broker(register)(7)==ESP_ERR_INVALID_STATE);
    finish_callback();
    assert(broker(unregister)(7)==ESP_OK && unregisters==calls+1);
    assert(status().generation==7 && status().orphan_callbacks==1);
    assert(broker(reset_after_deinit)(7));
    assert(broker(register)(7)==ESP_OK);
    /* Seed the production counter only to reach the boot-lifetime limit. */
    s_raw_tx.next_identity=UINT32_MAX;
    assert(submit(&token)==ESP_OK && token.identity==UINT32_MAX && status().identity_exhausted);
    driver_callback(&callback_info);retire(&token);
    assert(submit(&next)==ESP_ERR_INVALID_STATE && !next.identity);
    assert(broker(reset_after_deinit)(7) && broker(register)(7)==ESP_OK);
    assert(submit(&next)==ESP_ERR_INVALID_STATE && status().identity_exhausted);
    assert(broker(unregister)(7)==ESP_OK && broker(reset_after_deinit)(7));
    assert(allocations==frees);
    return 0;
}
'''
