"""Deferred production worker scheduling/ownership tests; AST only during implementation.

SDK/IPC/timer calls are controllable boundaries. The production worker itself
chooses sequencing, stores arguments, copies results and releases storage.
Does not qualify RF, driver scan internals or the future public Radio Session.
"""
import os
from pathlib import Path
import re
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]


def declarations(text):
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', text, flags=re.M)


class WPSWorker(unittest.TestCase):
    def test_production_worker_retains_ipc_and_orders_retirement(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        sdk_header = Path(sdk) / 'components/wpa_supplicant/esp_supplicant/include/esp_wps.h'
        internal = ROOT / 'components/esp32_mquickjs/internal'
        source = declarations((ROOT / 'components/esp32_mquickjs/src/modules/wifi_wps/esp32_mquickjs_wifi_wps_worker.c').read_text())
        # Real target builds check the 12-byte IPC ABI; this fixture runs with
        # host pointer widths and checks retained ownership and call ordering.
        source = source.replace('_Static_assert(sizeof(wps_ipc_config_t) == 12, "review WPS IPC ABI");', '')
        code = PRELUDE + declarations(sdk_header.read_text())
        code += declarations((internal / 'esp32_mquickjs_wifi_wps_sdk.h').read_text())
        code += declarations((internal / 'esp32_mquickjs_wifi_wps_worker.h').read_text())
        compile_run(self, code + BOUNDARIES + source + IMPLEMENTATIONS + MAIN)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FINISHED 0x10a
#define ESP_ERR_WIFI_NOT_INIT 0x3001
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define ESP_TIMER_TASK 0
typedef void *esp_timer_handle_t;
typedef struct {void (*callback)(void *);void *arg;int dispatch_method;const char *name;} esp_timer_create_args_t;
'''

BOUNDARIES = r'''
static unsigned allocations,start_calls,stop_calls,retire_calls,release_calls,copy_calls,commit_calls;
static unsigned creates,starts,stops,deletes;
static int ipc_mode,create_error,start_error,stop_error,delete_error,release_error;
static void *allocation;
static size_t allocation_size;
static esp_timer_create_args_t timer_args;
static bool timer_exists,timer_returned,timer_deleted;
static esp32_mquickjs_wifi_wps_native_status_t native;
static uint32_t callback_revision;
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n) {memset(p,0,n);}
static void *heap_caps_calloc(size_t count,size_t size,unsigned caps) {
    assert(count==1 && caps==3 && !allocations);
    allocation=calloc(count,size);assert(allocation);allocation_size=size;allocations++;return allocation;
}
static void heap_caps_free(void *p) {
    assert(p==allocation && allocations==1);
    for(size_t i=0;i<allocation_size;i++)assert(!((uint8_t *)p)[i]);
    free(p);allocation=NULL;allocations=0;
}
static esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    creates++;assert(args->dispatch_method==ESP_TIMER_TASK && !timer_exists);
    if(create_error)return create_error;timer_args=*args;timer_exists=true;*out=&timer_args;return ESP_OK;
}
static esp_err_t esp_timer_start_once(esp_timer_handle_t timer,uint64_t us) {
    starts++;assert(timer==&timer_args && timer_exists && us==1);
    if(start_error)return start_error;
    timer_returned=false;timer_deleted=false;return ESP_OK;
}
static esp_err_t esp_timer_stop_blocking(esp_timer_handle_t timer,uint32_t timeout) {
    stops++;assert(timer==&timer_args && timeout==1 && timer_returned);return stop_error;
}
static esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    deletes++;assert(timer==&timer_args && timer_exists);
    if(delete_error)return delete_error;timer_exists=false;timer_deleted=true;return ESP_OK;
}
esp_err_t esp32qjs_wps_native_begin(const esp_wps_config_t *config,uint64_t *identity) {
    assert(config->wps_type==WPS_TYPE_PIN && !*identity);*identity=41;
    native=(esp32_mquickjs_wifi_wps_native_status_t){.identity=41,.enabled=true};return ESP_OK;
}
esp_err_t esp32qjs_wps_native_start(uint64_t identity) {assert(identity==41);start_calls++;return ESP_OK;}
esp_err_t esp32qjs_wps_native_status(uint64_t identity,esp32_mquickjs_wifi_wps_native_status_t *out) {
    assert(identity==41);*out=native;return ESP_OK;
}
esp_err_t esp32qjs_wps_native_close(uint64_t identity) {
    assert(identity==41);stop_calls++;native.closing=true;native.inputs_stopped=true;
    native.credentials_available=false;return ESP_OK;
}
esp_err_t esp32qjs_wps_native_finish_capture(uint64_t identity) {
    assert(identity==41);stop_calls++;
    if(!native.terminal_seen)return ESP_ERR_INVALID_STATE;
    native.inputs_stopped=true;return ESP_OK;
}
esp_err_t esp32qjs_wps_native_retire_state(uint64_t identity) {
    assert(identity==41 && native.inputs_stopped && timer_returned && timer_deleted);
    retire_calls++;native.sdk_state_retired=true;return ESP_OK;
}
esp_err_t esp32qjs_wps_native_checkpoint(uint64_t identity,uint32_t *revision) {
    assert(identity==41 && native.sdk_state_retired);*revision=callback_revision;return ESP_OK;
}
esp_err_t esp32qjs_wps_native_release(uint64_t identity,uint32_t revision) {
    release_calls++;assert(identity==41 && !native.credentials_available && timer_deleted);
    if(release_error)return release_error;
    assert(revision==callback_revision);return ESP_OK;
}
esp_err_t esp32qjs_wps_native_pin_copy(uint64_t identity,uint8_t pin[8]) {
    assert(identity==41);memcpy(pin,"12345670",8);copy_calls++;return ESP_OK;
}
esp_err_t esp32qjs_wps_native_pin_commit(uint64_t identity) {assert(identity==41);commit_calls++;return ESP_OK;}
esp_err_t esp32qjs_wps_native_credentials_copy(uint64_t identity,esp32_mquickjs_wifi_wps_credentials_t *out) {
    assert(identity==41 && native.credentials_available);copy_calls++;
    memset(out,0,sizeof(*out));out->count=1;out->entries[0].ssid_length=3;memcpy(out->entries[0].ssid,"wps",3);
    return ESP_OK;
}
esp_err_t esp32qjs_wps_native_credentials_commit(uint64_t identity) {
    assert(identity==41 && native.credentials_available);commit_calls++;native.credentials_available=false;return ESP_OK;
}
'''

IMPLEMENTATIONS = r'''
static wps_ipc_config_t pending;
int esp_wifi_ipc_internal(wps_ipc_config_t *config,bool sync) {
    assert(sync && !config->arg_size && config->arg==allocation);
    if(ipc_mode==1)return ESP_ERR_NO_MEM;
    if(ipc_mode==2) {pending=*config;return 909;}
    return config->fn(config->arg);
}
static void fire_timer(void) {
    assert(timer_exists);timer_args.callback(timer_args.arg);timer_returned=true;
}
static void reset_boundaries(void) {
    assert(!allocations && !timer_exists);
    start_calls=stop_calls=retire_calls=release_calls=copy_calls=commit_calls=0;
    creates=starts=stops=deletes=0;
    ipc_mode=create_error=start_error=stop_error=delete_error=release_error=0;
    timer_returned=timer_deleted=false;memset(&native,0,sizeof(native));callback_revision=0;
}
'''

MAIN = r'''
int main(void) {
    esp_wps_config_t config={.wps_type=WPS_TYPE_PIN};
    esp32_mquickjs_wifi_wps_worker_t *w=NULL;
    memcpy(config.pin,"12345671",9);
    assert(esp32_mquickjs_wifi_wps_worker_create(&config,&w)==ESP_ERR_INVALID_ARG && !allocations);
    memcpy(config.pin,"12345670",9);
    memset(config.factory_info.manufacturer,'x',sizeof(config.factory_info.manufacturer));
    assert(esp32_mquickjs_wifi_wps_worker_create(&config,&w)==ESP_ERR_INVALID_ARG && !allocations);
    config.factory_info.manufacturer[0]=0;
    assert(esp32_mquickjs_wifi_wps_worker_create(&config,&w)==ESP_OK);
    assert(esp32_mquickjs_wifi_wps_worker_start(w)==ESP_ERR_INVALID_STATE && !start_calls);
    assert(esp32_mquickjs_wifi_wps_worker_prepare(w)==ESP_OK && !start_calls);
    assert(esp32_mquickjs_wifi_wps_worker_prepare(w)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_wps_worker_start(w)==ESP_OK && start_calls==1);
    assert(esp32_mquickjs_wifi_wps_worker_release(&w)==ESP_ERR_INVALID_STATE && allocations==1);
    for(size_t i=0;i<sizeof(w->config);i++)assert(!((uint8_t *)&w->config)[i]);
    uint8_t pin[8];
    assert(esp32_mquickjs_wifi_wps_worker_pin(w,pin,false)==ESP_OK && !memcmp(pin,"12345670",8));
    assert(!commit_calls);
    for(size_t i=0;i<sizeof(w->result);i++)assert(!((uint8_t *)&w->result)[i]);
    assert(esp32_mquickjs_wifi_wps_worker_pin(w,NULL,true)==ESP_OK && commit_calls==1);
    assert(esp32_mquickjs_wifi_wps_worker_finish_capture(w)==ESP_ERR_INVALID_STATE);
    native.terminal_seen=true;native.credentials_available=true;
    create_error=301;
    assert(esp32_mquickjs_wifi_wps_worker_finish_capture(w)==301 && !retire_calls);
    create_error=0;start_error=302;
    unsigned completed_stops=stop_calls;
    assert(esp32_mquickjs_wifi_wps_worker_finish_capture(w)==302 && stop_calls==completed_stops);
    start_error=0;
    assert(esp32_mquickjs_wifi_wps_worker_finish_capture(w)==ESP_ERR_NOT_FINISHED && !retire_calls);
    fire_timer();stop_error=303;
    assert(esp32_mquickjs_wifi_wps_worker_finish_capture(w)==303 && !retire_calls);
    stop_error=0;delete_error=304;
    assert(esp32_mquickjs_wifi_wps_worker_finish_capture(w)==304 && !retire_calls);
    unsigned stopped=stops;
    delete_error=0;
    assert(esp32_mquickjs_wifi_wps_worker_finish_capture(w)==ESP_OK && retire_calls==1 && stops==stopped);
    esp32_mquickjs_wifi_wps_credentials_t credentials;
    assert(esp32_mquickjs_wifi_wps_worker_credentials(w,&credentials,false)==ESP_OK);
    assert(credentials.count==1 && !memcmp(credentials.entries[0].ssid,"wps",3));
    assert(native.credentials_available);
    assert(esp32_mquickjs_wifi_wps_worker_credentials(w,NULL,true)==ESP_OK && !native.credentials_available);
    release_error=ESP_ERR_NOT_FINISHED;
    assert(esp32_mquickjs_wifi_wps_worker_close(w)==ESP_ERR_NOT_FINISHED && !w->capture_retired);
    release_error=0;
    assert(esp32_mquickjs_wifi_wps_worker_close(w)==ESP_ERR_NOT_FINISHED && timer_exists);
    fire_timer();
    assert(esp32_mquickjs_wifi_wps_worker_close(w)==ESP_OK && w->retired && !w->identity);
    assert(esp32_mquickjs_wifi_wps_worker_close(w)==ESP_OK);
    assert(esp32_mquickjs_wifi_wps_worker_release(&w)==ESP_OK && !w && !allocations);
    reset_boundaries();

    /* Definite pre-handoff OOM permits cleanup of an owner with no native ID. */
    assert(esp32_mquickjs_wifi_wps_worker_create(&config,&w)==ESP_OK);ipc_mode=1;
    assert(esp32_mquickjs_wifi_wps_worker_prepare(w)==ESP_ERR_NO_MEM && !w->handoff_unknown);
    assert(esp32_mquickjs_wifi_wps_worker_close(w)==ESP_OK && !creates);
    assert(esp32_mquickjs_wifi_wps_worker_release(&w)==ESP_OK);
    reset_boundaries();

    /* An unconfirmed copy must retain its output and surface the raw IPC
     * error even when start itself succeeded. */
    assert(esp32_mquickjs_wifi_wps_worker_create(&config,&w)==ESP_OK);
    assert(esp32_mquickjs_wifi_wps_worker_prepare(w)==ESP_OK);
    assert(esp32_mquickjs_wifi_wps_worker_start(w)==ESP_OK);
    ipc_mode=2;
    assert(esp32_mquickjs_wifi_wps_worker_pin(w,pin,false)==909 && w->handoff_unknown);
    esp32_mquickjs_wifi_wps_worker_status_t copy_status;
    assert(esp32_mquickjs_wifi_wps_worker_status(w,&copy_status)==ESP_OK && copy_status.error==909);
    assert(pending.fn(pending.arg)==ESP_OK && !memcmp(w->result.pin,"12345670",8));
    assert(esp32_mquickjs_wifi_wps_worker_release(&w)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wireless_secure_zero(w,sizeof(*w));heap_caps_free(w);w=NULL;
    reset_boundaries();

    /* Unknown handoff retains argument/result storage, including after a late
     * receipt. It does not create an automatic retry or recovery policy. */
    assert(esp32_mquickjs_wifi_wps_worker_create(&config,&w)==ESP_OK);ipc_mode=2;
    assert(esp32_mquickjs_wifi_wps_worker_prepare(w)==909 && w->handoff_unknown);
    esp32_mquickjs_wifi_wps_worker_status_t status;
    w->native.identity=UINT64_MAX;
    assert(esp32_mquickjs_wifi_wps_worker_status(w,&status)==ESP_OK && !status.native.identity && status.handoff_unknown);
    assert(esp32_mquickjs_wifi_wps_worker_close(w)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_wps_worker_release(&w)==ESP_ERR_INVALID_STATE && allocations==1);
    assert(pending.fn(pending.arg)==ESP_OK);
    assert(esp32_mquickjs_wifi_wps_worker_status(w,&status)==ESP_OK && !status.native.identity && status.handoff_unknown);
    assert(esp32_mquickjs_wifi_wps_worker_release(&w)==ESP_ERR_INVALID_STATE);
    /* Fixture disposal after the simulated task returned, not production
     * close or proof of device/runtime recovery from an unknown handoff. */
    esp32_mquickjs_wireless_secure_zero(w,sizeof(*w));heap_caps_free(w);
    return 0;
}
'''
