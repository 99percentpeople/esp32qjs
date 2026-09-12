"""Production NAN Radio cleanup suffix; fixtures run at Wi-Fi phase validation."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'components/esp32_mquickjs'


def function(source, name):
    match = re.search(r'^(?:static )?[\w *]+\b' + re.escape(name) + r'\([^;{}]*\)\n\{', source, re.M)
    if match is None:
        raise AssertionError(name)
    return source[match.start():source.index('\n}\n', match.end()) + 3]


class NanRadioRetirement(unittest.TestCase):
    def test_actual_close_keeps_exclusion_through_delayed_stop_detach_and_observer(self):
        self.compile_case(MAIN)

    def test_actual_service_mutation_token_and_cancel_suffix(self):
        self.compile_case(SERVICE_MAIN)

    def compile_case(self, main):
        compiler = shutil.which('cc')
        if not compiler:
            self.skipTest('Host C compiler unavailable')
        header = (BASE / 'internal/esp32_mquickjs_wifi_nan_radio.h').read_text()
        status = header[header.index('typedef struct {'):header.index('/* Background task')]
        production = (BASE / 'src/modules/wifi_radio/esp32_mquickjs_wifi_nan_radio.inc').read_text()
        start = production.index('struct wifi_radio_nan {')
        binding = production[start:production.index('\n};', start) + 4]
        source = PREFIX + status + binding + BOUNDARY + SERVICE_BOUNDARY
        source += function(production, 'wifi_radio_nan_exact_locked')
        source += function(production, 'esp32_mquickjs_wifi_radio_nan_close')
        source += function(production, 'esp32_mquickjs_wifi_radio_nan_service_start')
        source += function(production, 'esp32_mquickjs_wifi_radio_nan_service_close')
        source += main
        with tempfile.TemporaryDirectory() as folder:
            path, executable = Path(folder) / 'case.c', Path(folder) / 'case'
            path.write_text(source)
            result = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                str(path), '-o', str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t, wifi_mode_t, wifi_storage_t;
typedef struct {int sentinel;} esp_netif_t;
typedef struct {uint32_t generation,identity;int client;bool acquired;} esp32_mquickjs_wifi_radio_lease_t;
typedef struct {uint32_t generation,lease_identity,identity;int kind;} esp32_mquickjs_wifi_radio_operation_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 11
#define ESP_ERR_INVALID_STATE 12
#define ESP_ERR_INVALID_RESPONSE 13
#define ESP_ERR_TIMEOUT 14
#define ESP_ERR_WIFI_NOT_STARTED 15
#define ESP_ERR_NOT_SUPPORTED 16
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
#define WIFI_MODE_NAN 4
#define WIFI_MODE_STA 1
#define RADIO_EVENTS_IDLE 0
#define RADIO_EVENTS_STOP 2
#define ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED 0
#define ESP32_MQUICKJS_WIFI_RADIO_STOPPED 1
#define ESP32_MQUICKJS_WIFI_RADIO_STOPPING 2
#define ESP32_MQUICKJS_WIFI_RADIO_OPERATION_NAN 3
typedef struct wifi_radio_nan wifi_radio_nan_t;
'''

BOUNDARY = r'''
static wifi_radio_nan_t *s_nan_radio;
static struct {
    int lock,driver_state;
    uint32_t generation;
    esp32_mquickjs_wifi_radio_operation_t operation;
    unsigned event_phase,event_live;
    bool stop_submitted,started,stop_required,restart_required,driver_owned;
    const char *fault_stage,*cleanup_stage;
    esp_err_t fault_error,cleanup_error;
    wifi_mode_t effective_mode;
    wifi_storage_t storage;
} s_radio;
static bool mutation_locked, critical;
static unsigned stops, waits, resets, detaches, unobserves, mode_writes, storage_writes, releases, frees;
static int wait_error, detach_error, observer_error, mode_error, storage_error;
static wifi_mode_t native_mode=4;
static esp_netif_t netif;
#define taskENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=true;}while(0)
#define taskEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=false;}while(0)
static void wifi_radio_operation_lock(void){assert(!mutation_locked);mutation_locked=true;}
static void wifi_radio_operation_unlock(void){assert(mutation_locked&&!critical);mutation_locked=false;}
static bool wifi_radio_lease_valid(const esp32_mquickjs_wifi_radio_lease_t *lease){
    return lease->acquired&&lease->generation==s_radio.generation&&lease->identity==s_radio.operation.lease_identity;
}
static void sdk_boundary(void){assert(mutation_locked&&!critical&&s_nan_radio&&s_radio.operation.identity);}
static int service_callback_error,tx_error;
static unsigned tx_seals,tx_closes;
static int esp32_mquickjs_wifi_nan_sdk_service_quiesce(uint8_t id){sdk_boundary();assert(!id);return service_callback_error;}
static void esp32_mquickjs_wifi_nan_tx_seal(void){sdk_boundary();++tx_seals;}
static int esp32_mquickjs_wifi_nan_tx_close(void){sdk_boundary();++tx_closes;return tx_error;}
static int wifi_radio_begin_events(unsigned phase,wifi_mode_t expected){sdk_boundary();assert(expected==4);s_radio.event_phase=phase;return ESP_OK;}
static void wifi_radio_set_state(int state){s_radio.driver_state=state;}
static int esp_wifi_stop(void){sdk_boundary();++stops;return ESP_OK;}
static int wifi_radio_wait_events_inner(void *runtime){sdk_boundary();assert(!runtime);++waits;if(!wait_error){s_radio.event_phase=0;s_radio.event_live=0;}return wait_error;}
static int esp32_mquickjs_wifi_nan_sdk_sync_reset(void){sdk_boundary();assert(!s_radio.started);++resets;return ESP_OK;}
static int esp32_mquickjs_wifi_netif_retire(esp_netif_t **p,int *error){sdk_boundary();assert(resets&&*p==&netif&&!*error);++detaches;if(!detach_error)*p=NULL;return detach_error;}
static int esp32_mquickjs_wifi_nan_sdk_unobserve(uint32_t *identity){sdk_boundary();assert(!s_nan_radio->netif&&*identity==9);++unobserves;if(!observer_error)*identity=0;return observer_error;}
static int esp_wifi_get_mode(wifi_mode_t *mode){sdk_boundary();*mode=native_mode;return ESP_OK;}
static int esp_wifi_set_mode(wifi_mode_t mode){sdk_boundary();assert(!s_nan_radio->observer);++mode_writes;native_mode=mode;return mode_error;}
static int esp_wifi_set_storage(wifi_storage_t storage){sdk_boundary();assert(storage==0);++storage_writes;return storage_error;}
static int wifi_radio_cleanup_fault(const char *stage,int error){sdk_boundary();s_radio.cleanup_stage=stage;s_radio.cleanup_error=error;return error;}
static void wifi_radio_release_locked(esp32_mquickjs_wifi_radio_lease_t *lease){assert(mutation_locked&&!critical&&!s_radio.operation.identity);++releases;memset(lease,0,sizeof(*lease));}
static void esp32_mquickjs_memory_payload_free(void *p){assert(mutation_locked&&!critical&&!s_nan_radio&&releases);++frees;free(p);}
'''

MAIN = r'''
int main(void) {
    s_radio.generation=7;s_radio.driver_owned=true;s_radio.started=true;s_radio.stop_required=true;
    s_radio.effective_mode=4;s_radio.event_live=4;
    s_radio.operation=(esp32_mquickjs_wifi_radio_operation_t){7,3,5,ESP32_MQUICKJS_WIFI_RADIO_OPERATION_NAN};
    s_nan_radio=calloc(1,sizeof(*s_nan_radio));assert(s_nan_radio);
    s_nan_radio->lease=(esp32_mquickjs_wifi_radio_lease_t){7,3,0,true};
    s_nan_radio->status.operation=s_radio.operation;
    s_nan_radio->status.start_attempted=s_nan_radio->status.start_accepted=true;
    s_nan_radio->netif=&netif;s_nan_radio->observer=9;
    s_nan_radio->prepare_attempted=s_nan_radio->saved=s_nan_radio->storage_attempted=true;
    s_nan_radio->tx_owned=true;
    s_nan_radio->saved_mode=1;s_nan_radio->saved_storage=0;
    esp32_mquickjs_wifi_radio_operation_t token=s_radio.operation,stale=token;
    esp32_mquickjs_wifi_nan_radio_status_t status={0};
    --stale.identity;
    assert(esp32_mquickjs_wifi_radio_nan_close(&stale,&status)==ESP_ERR_INVALID_STATE&&!stops&&!frees);
    stale=token;--stale.generation;
    assert(esp32_mquickjs_wifi_radio_nan_close(&stale,&status)==ESP_ERR_INVALID_STATE&&!stops);
    service_callback_error=ESP_ERR_TIMEOUT;
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==ESP_ERR_TIMEOUT&&!stops&&token.identity&&tx_seals==1);
    service_callback_error=0;
    wait_error=ESP_ERR_TIMEOUT;
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==ESP_ERR_TIMEOUT&&token.identity&&stops==1&&!resets);
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==ESP_ERR_TIMEOUT&&stops==1&&waits==2);
    wait_error=0;tx_error=ESP_ERR_TIMEOUT;
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==ESP_ERR_TIMEOUT&&stops==1&&!resets&&tx_closes==1);
    assert(s_nan_radio->tx_owned&&token.identity&&!releases);
    tx_error=0;detach_error=ESP_ERR_TIMEOUT;
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==ESP_ERR_TIMEOUT&&stops==1&&resets==1&&detaches==1&&!frees);
    detach_error=0;observer_error=ESP_ERR_TIMEOUT;
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==ESP_ERR_TIMEOUT&&resets==1&&detaches==2&&unobserves==1&&!mode_writes);
    observer_error=0;mode_error=-71;
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==-71&&token.identity&&mode_writes==1&&!releases);
    /* A failed setter that nevertheless reached the desired mode is read back
     * on retry; it is not submitted twice. Original failure was returned above. */
    mode_error=0;
    stale=token;
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==ESP_OK&&!token.identity&&!s_nan_radio);
    assert(stops==1&&resets==1&&detaches==2&&unobserves==2&&mode_writes==1&&storage_writes==1&&releases==1&&frees==1);
    assert(status.stopped&&status.native_reset&&status.netif_retired&&status.observer_retired&&status.mode_restored&&status.storage_restored);
    assert(!s_radio.cleanup_stage&&s_radio.effective_mode==1&&!s_radio.started&&!s_radio.stop_required);
    assert(esp32_mquickjs_wifi_radio_nan_close(&stale,&status)==ESP_ERR_INVALID_STATE&&frees==1);
}
'''

SERVICE_BOUNDARY = r'''
typedef struct{int marker;}wifi_nan_publish_cfg_t,wifi_nan_subscribe_cfg_t;
static unsigned publish_calls,cancel_calls;static int cancel_error,resume_error;static bool tx_drained;
static int esp32_mquickjs_wifi_nan_sdk_publish(const wifi_nan_publish_cfg_t*p,uint8_t*id){sdk_boundary();assert(p&&id&&!*id);++publish_calls;*id=7;return 0;}
static int esp32_mquickjs_wifi_nan_sdk_subscribe(const wifi_nan_subscribe_cfg_t*p,uint8_t*id){return esp32_mquickjs_wifi_nan_sdk_publish(p,id);}
static int esp32_mquickjs_wifi_nan_sdk_cancel_service(uint8_t id){sdk_boundary();assert(id==7);++cancel_calls;return cancel_error;}
static bool esp32_mquickjs_wifi_nan_tx_service_drained(uint8_t id){sdk_boundary();assert(id==7);return tx_drained;}
static int esp32_mquickjs_wifi_nan_sdk_service_resume(uint8_t id){sdk_boundary();assert(id==7);return resume_error;}
'''
SERVICE_MAIN = r'''
int main(void){
 s_radio.generation=7;s_radio.operation=(esp32_mquickjs_wifi_radio_operation_t){7,3,5,ESP32_MQUICKJS_WIFI_RADIO_OPERATION_NAN};
 s_nan_radio=calloc(1,sizeof(*s_nan_radio));assert(s_nan_radio);
 s_nan_radio->lease=(esp32_mquickjs_wifi_radio_lease_t){7,3,0,true};s_nan_radio->status.ready=true;
 esp32_mquickjs_wifi_radio_operation_t token=s_radio.operation,stale=token;--stale.identity;
 wifi_nan_publish_cfg_t config={1};uint8_t id=0;
 assert(esp32_mquickjs_wifi_radio_nan_service_start(&stale,&config,NULL,&id)==ESP_ERR_INVALID_STATE&&!id&&!publish_calls);
 assert(!esp32_mquickjs_wifi_radio_nan_service_start(&token,&config,NULL,&id)&&id==7&&publish_calls==1);
 bool cancelled=false;cancel_error=-71;
 assert(esp32_mquickjs_wifi_radio_nan_service_close(&token,id,&cancelled)==-71&&!cancelled&&cancel_calls==1);
 cancel_error=0;assert(esp32_mquickjs_wifi_radio_nan_service_close(&token,id,&cancelled)==ESP_ERR_TIMEOUT&&cancelled&&cancel_calls==2);
 assert(esp32_mquickjs_wifi_radio_nan_service_close(&token,id,&cancelled)==ESP_ERR_TIMEOUT&&cancel_calls==2);
 tx_drained=true;resume_error=-72;assert(esp32_mquickjs_wifi_radio_nan_service_close(&token,id,&cancelled)==-72&&cancel_calls==2);
 resume_error=0;assert(!esp32_mquickjs_wifi_radio_nan_service_close(&token,id,&cancelled)&&cancel_calls==2);
 assert(esp32_mquickjs_wifi_radio_nan_service_close(&stale,id,&cancelled)==ESP_ERR_INVALID_STATE&&cancel_calls==2);
 free(s_nan_radio);assert(!mutation_locked&&!critical);
}
'''
