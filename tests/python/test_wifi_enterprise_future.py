"""Deferred production Enterprise worker/poll/cancel and config/profile lifetime.

No alternative state machine: uses actual driver helpers plus config/profile.
SDK Radio calls and background queue are controlled boundaries. Full Future core
scheduling and real VM result conversion remain separate stage requirements.
"""
from pathlib import Path
import unittest
from test_wifi_enterprise_profile import PRELUDE
from test_wifi_rx_target import unit, INTERNAL
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import ROOT, CORE, extract


class WiFiEnterpriseFuture(unittest.TestCase):
    def test_queue_cancel_late_sdk_return_and_cleanup_retry(self):
        code = PRELUDE.replace('static bool critical,allocation_fail;', 'static unsigned critical;static bool allocation_fail;')
        code = code.replace('assert(!critical);critical=true;', 'assert(critical<2);critical++;')
        code = code.replace('assert(critical);critical=false;', 'assert(critical);critical--;')
        code += '\n#include <stdatomic.h>\n#define ESP_ERR_INVALID_STATE 5\n#define CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT 1\n'
        sdk = Path('/home/zach/esp/esp-idf/components/wpa_supplicant/esp_supplicant/include/esp_eap_client.h')
        code += unit(sdk) + unit(INTERNAL / 'esp32_mquickjs_wifi_enterprise_profile.h')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_eap_config.h')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_eap_sdk.h')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_eap_install.h')
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        folder = ROOT / 'components/esp32_mquickjs/src/modules/wifi_enterprise'
        code += unit(folder / 'esp32_mquickjs_wifi_enterprise_profile.c')
        code += 'static uint64_t binding;\nstatic uint64_t esp32_mquickjs_wifi_radio_eap_identity(void){assert(!critical);return binding;}\n'
        code += unit(folder / 'esp32_mquickjs_wifi_eap_config.c')
        code += TYPES
        source = (folder / 'esp32_mquickjs_wifi_enterprise.c').read_text()
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start) + 3]
        code += BOUNDARIES + "\n#define heap_caps_free free\n"
        for name in ['enterprise_worker', 'enterprise_start', 'enterprise_poll',
                     'enterprise_cancel', 'enterprise_destroy', 'enterprise_timeout']:
            code += extract(source, name)
        compile_run(self, code + MAIN)


TYPES = r'''
typedef int JSContext,esp32_mquickjs_runtime_t;
typedef unsigned esp32_mquickjs_future_token_t;
typedef struct {uint32_t generation,identity;int client;bool acquired;} esp32_mquickjs_wifi_radio_lease_t;
typedef enum {ESP32_MQUICKJS_FUTURE_PENDING,ESP32_MQUICKJS_FUTURE_READY} esp32_mquickjs_future_poll_t;
typedef enum {ESP32_MQUICKJS_CANCEL_REQUESTED} esp32_mquickjs_cancel_result_t;
typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;
'''
BOUNDARIES = r'''
static struct {void (*fn)(void *);void *arg;} job;
static bool queue_full,disconnect_ready=true,ready=true,cancel_inside_sdk;
static unsigned installs,clears,disconnects;
static esp_err_t clear_error,helper_error;
static esp32_mquickjs_future_driver_state_t *active;
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg){
    assert(!critical);if(queue_full || job.fn)return false;job.fn=fn;job.arg=arg;return true;
}
static void run_job(void){
    assert(job.fn);void (*fn)(void *)=job.fn;void *arg=job.arg;job.fn=NULL;job.arg=NULL;fn(arg);
}
static bool esp32_mquickjs_wifi_radio_eap_ready(void){return ready;}
static esp_err_t esp32_mquickjs_wifi_eap_capture_owners(esp32_mquickjs_wifi_radio_lease_t owners[3]){
    owners[0]=(esp32_mquickjs_wifi_radio_lease_t){1,77,0,true};return helper_error;
}
static esp_err_t esp32_mquickjs_wifi_radio_eap_status(uint64_t id,esp32_mquickjs_wifi_eap_install_result_t *r){
    assert(id==binding && id);r->enabled=ready;return ESP_OK;
}
static esp_err_t esp32_mquickjs_wifi_radio_eap_install(const esp32_mquickjs_wifi_radio_lease_t *a,
    const esp32_mquickjs_wifi_radio_lease_t *s,const esp32_mquickjs_wifi_radio_lease_t *ap,
    esp32_mquickjs_wifi_eap_profile_t *profile,esp32_mquickjs_wifi_eap_install_result_t *r){
    (void)s;(void)ap;assert(!critical && a->identity==77 && profile && !binding);installs++;
    if(cancel_inside_sdk)atomic_store(&active->cancelled,true);
    binding=100;ready=true;r->enabled=true;return ESP_OK;
}
static esp_err_t esp32_mquickjs_wifi_radio_eap_clear(uint64_t id,esp32_mquickjs_wifi_eap_install_result_t *r){
    (void)r;assert(!critical && id && id==binding);clears++;if(!clear_error)binding=0;return clear_error;
}
static esp_err_t esp32_mquickjs_wifi_eap_disconnect_ready(bool *out){
    assert(binding);disconnects++;*out=disconnect_ready;return ESP_OK;
}
'''
MAIN = r'''
static void configure(void){
    uint8_t byte=42;esp32_mquickjs_wifi_eap_input_t input={.policy={.methods=ESP_EAP_TYPE_PEAP}};
    input.fields[ESP32_MQUICKJS_WIFI_EAP_USERNAME]=(esp32_mquickjs_wifi_eap_span_t){&byte,1};
    esp32_mquickjs_wifi_eap_profile_t *p=NULL;
    assert(esp32_mquickjs_wifi_eap_profile_create(&input,&p)==ESP_OK);
    assert(esp32_mquickjs_wifi_eap_config_replace(s_eap_config.revision,p)==ESP_OK);
    esp32_mquickjs_wifi_eap_profile_release(p);
}
static esp32_mquickjs_future_driver_state_t *begin(esp32_mquickjs_wifi_eap_config_action_t action){
    esp32_mquickjs_future_driver_state_t *s=calloc(1,sizeof(*s));assert(s);
    s->action=action;s->timeout_ms=5000;atomic_init(&s->done,false);atomic_init(&s->cancelled,false);
    assert(esp32_mquickjs_wifi_eap_config_begin(s_eap_config.revision,action,&s->control,&s->profile)==ESP_OK);
    active=s;return s;
}
static void finish(esp32_mquickjs_future_driver_state_t *s){
    assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_READY);enterprise_destroy(s);
    assert(!s_eap_config.operation.identity);
}
int main(void){
    assert(esp32_mquickjs_wifi_eap_config_open()==ESP_OK);configure();
    esp32_mquickjs_future_driver_state_t *s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE);
    assert(enterprise_timeout(s)==5000);enterprise_destroy(s); /* Captured but never started. */
    assert(s_eap_config.configured && !s_eap_config.operation.identity && !installs);
    s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE);assert(enterprise_start(NULL,NULL,0,s));
    queue_full=true;finish(s);queue_full=false;assert(!binding && !installs);
    s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE);assert(enterprise_start(NULL,NULL,0,s));
    assert(enterprise_cancel(s)==ESP32_MQUICKJS_CANCEL_REQUESTED);finish(s);assert(!installs);
    s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE);assert(enterprise_start(NULL,NULL,0,s));
    assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING);
    enterprise_cancel(s);assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(s_eap_config.operation_profile && s_eap_config.operation.identity);
    run_job();finish(s);assert(!installs); /* Queued worker observed cancellation. */
    s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE);assert(enterprise_start(NULL,NULL,0,s));
    assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING);cancel_inside_sdk=true;
    run_job();cancel_inside_sdk=false;
    assert(binding && s_eap_config.operation.identity);
    finish(s);assert(installs==1 && binding && s_eap_config.configured);

    s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_CLEAR);assert(enterprise_start(NULL,NULL,0,s));
    disconnect_ready=false;assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING && !job.fn);
    enterprise_cancel(s);finish(s);assert(binding && !clears);
    disconnect_ready=true;clear_error=ESP_ERR_INVALID_STATE;
    s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_CLEAR);assert(enterprise_start(NULL,NULL,0,s));
    assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING);run_job();finish(s);
    assert(binding && s_eap_config.configured && clears==1);
    clear_error=ESP_OK;s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_CLEAR);assert(enterprise_start(NULL,NULL,0,s));
    assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING);run_job();finish(s);
    assert(!binding && !s_eap_config.configured && clears==2);
    configure();unsigned old_disconnects=disconnects;
    s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_DISABLE);assert(enterprise_start(NULL,NULL,0,s));
    assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING);run_job();finish(s);
    assert(s_eap_config.configured && disconnects==old_disconnects);
    esp32_mquickjs_wifi_eap_config_begin_close();assert(esp32_mquickjs_wifi_eap_config_finish_close());
    assert(!s_eap_profile_counts.profiles && !s_eap_profile_counts.reserved_bytes && !critical);
    return 0;
}
'''
