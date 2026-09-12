"""Deferred production AP Radio admission; worker and regulatory boundaries injected."""
from pathlib import Path
import unittest
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_wps_ap_radio.inc'


class WpsAPBandRadio(unittest.TestCase):
    def test_regulatory_five_ghz_admission_and_rejected_channels_do_not_start_native(self):
        compile_run(self, TYPES + extract(SOURCE.read_text(), 'esp32_mquickjs_wifi_radio_wps_ap_begin') + MAIN)


TYPES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_NOT_SUPPORTED 4
#define WIFI_IF_AP 1
#define WIFI_MODE_STA 1
#define WIFI_RADIO_MAX_LEASES 2
#define WIFI_RADIO_WPS_PENDING false
#define WIFI_RADIO_SMARTCONFIG_PENDING false
#define WIFI_RADIO_EAP_PENDING false
#define ESP32_MQUICKJS_WIFI_RADIO_OPERATION_WPS 8
#define ESP32_MQUICKJS_MEMORY_DEFAULT 0
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 0
typedef int esp_err_t;
typedef int wifi_second_chan_t;
typedef int esp_wps_config_t;
typedef struct {unsigned identity;bool acquired;} esp32_mquickjs_wifi_radio_lease_t;
typedef struct {unsigned identity,generation,lease_identity,kind;} esp32_mquickjs_wifi_radio_operation_t;
typedef struct {int unused;} esp32_mquickjs_wifi_wps_ap_worker_t;
typedef struct {const char *stage;int error;} esp32_mquickjs_wifi_wps_ap_radio_status_t;
typedef struct {unsigned owners[3];esp32_mquickjs_wifi_wps_ap_worker_t *worker;const char *stage;int error;} wifi_radio_wps_ap_t;
static wifi_radio_wps_ap_t *s_wps_ap_radio;
static struct {struct {unsigned identity;} owner;bool uncertain;} s_interval;
static struct {int lock,effective_mode;unsigned generation,next_operation_identity;struct {unsigned identity;bool fixed_channel;}leases[2];esp32_mquickjs_wifi_radio_operation_t operation;} s_radio;
static unsigned s_wps_fence_posted,s_wps_fence_seen;
static bool locked,critical,allow_five;
static unsigned primary,prepared,started,closed,released,regulatory;
static esp32_mquickjs_wifi_wps_ap_worker_t worker;
static int esp32_mquickjs_wifi_wps_ap_worker_create(const esp_wps_config_t *c,esp32_mquickjs_wifi_wps_ap_worker_t **w){assert(c);*w=&worker;return 0;}
static int esp32_mquickjs_wifi_wps_ap_worker_prepare(esp32_mquickjs_wifi_wps_ap_worker_t *w){assert(w==&worker && locked && !critical);++prepared;return 0;}
static int esp32_mquickjs_wifi_wps_ap_worker_start(esp32_mquickjs_wifi_wps_ap_worker_t *w){assert(w==&worker && prepared);++started;return 0;}
static int esp32_mquickjs_wifi_wps_ap_worker_close(esp32_mquickjs_wifi_wps_ap_worker_t *w){assert(w==&worker);++closed;return 0;}
static int esp32_mquickjs_wifi_wps_ap_worker_release(esp32_mquickjs_wifi_wps_ap_worker_t **w){assert(*w==&worker && closed);*w=NULL;++released;return 0;}
static void wifi_radio_operation_lock(void){assert(!locked);locked=true;}
static void wifi_radio_operation_unlock(void){assert(locked && !critical);locked=false;}
#define taskENTER_CRITICAL(p) do {(void)p;assert(locked && !critical);critical=true;}while(0)
#define taskEXIT_CRITICAL(p) do {(void)p;assert(critical);critical=false;}while(0)
static int wifi_radio_connection_owner_locked(const esp32_mquickjs_wifi_radio_lease_t *a,const esp32_mquickjs_wifi_radio_lease_t *s,const esp32_mquickjs_wifi_radio_lease_t *p,int iface,bool observer){(void)a;(void)s;assert(locked && p->acquired && iface==WIFI_IF_AP && !observer);return 0;}
static int wifi_radio_get_channel_locked(uint8_t *p,wifi_second_chan_t *s,uint32_t *g){*p=primary;*s=0;*g=s_radio.generation;return 0;}
static int wifi_radio_validate_regulatory_channel(uint8_t p){assert(locked);++regulatory;return p==6 || (allow_five && p==36)?ESP_OK:ESP_ERR_NOT_SUPPORTED;}
static void *esp32_mquickjs_memory_wireless_calloc(const char *module,size_t n,size_t size,int type,int budget){(void)module;(void)type;(void)budget;assert(locked && !critical);return calloc(n,size);}
static void esp32_mquickjs_memory_payload_free(void *p){free(p);}
static int wifi_radio_wps_ap_snapshot_locked(const esp32_mquickjs_wifi_radio_operation_t *t,esp32_mquickjs_wifi_wps_ap_radio_status_t *s){assert(t->identity && s_wps_ap_radio);s->error=s_wps_ap_radio->error;return 0;}
'''


MAIN = r'''
int main(void) {
    const unsigned channels[]={0,6,36,255};
    esp32_mquickjs_wifi_radio_lease_t owners[3]={{1,true},{2,true},{3,true}};
    esp_wps_config_t config=0;
    for(unsigned support=0;support<2;++support)for(unsigned i=0;i<4;++i) {
        assert(!s_wps_ap_radio && !locked && !critical);
        primary=channels[i];allow_five=support;s_radio.generation=7;s_radio.next_operation_identity=11;
        prepared=started=closed=released=regulatory=0;
        esp32_mquickjs_wifi_radio_operation_t token={0};
        esp32_mquickjs_wifi_wps_ap_radio_status_t status;
        bool accepted=primary==6 || (support && primary==36);
        assert(esp32_mquickjs_wifi_radio_wps_ap_begin(owners,&config,&token,&status)==(accepted?ESP_OK:ESP_ERR_NOT_SUPPORTED));
        assert(regulatory==1 && !locked && !critical);
        if(accepted) {
            assert(token.identity==11 && token.generation==7 && token.lease_identity==3);
            assert(prepared==1 && started==1 && !closed && !released && s_wps_ap_radio);
            free(s_wps_ap_radio);s_wps_ap_radio=NULL; /* Worker/close retirement has separate production fixtures. */
        } else assert(!token.identity && !prepared && !started && closed==1 && released==1 && !s_wps_ap_radio);
    }
    return 0;
}
'''
