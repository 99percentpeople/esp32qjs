"""Late Station attachment executes the production initialization suffix."""
from pathlib import Path
import unittest
from wireless_vm_fixture import extract
from test_wireless_control_regression import compile_run

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c'

class WiFiLateStationNetif(unittest.TestCase):
    def test_started_radio_initializes_stack_before_publishing_station(self):
        source=SOURCE.read_text()
        init=extract(source,'wifi_init_helper')
        start=init.index('    if (esp32_mquickjs_wifi_radio_get_status(&radio_status)')
        stop=init.index('    s_wifi_setup_stage = NULL;',start)
        code=BOUNDARY
        if 'static esp_err_t wifi_start_existing_station_netif(' in source:
            code+=extract(source,'wifi_start_existing_station_netif')
        # Exact production suffix, including the shared fail label. SDK/RTOS
        # calls alone are injected; no substitute initialization state machine.
        code+='static int initialize_suffix(void) { esp32_mquickjs_wifi_radio_status_t radio_status;int err=0;\n'+init[start:stop]+'\nreturn 0; fail:return err;}\n'
        compile_run(self,code+MAIN)

BOUNDARY=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define WIFI_MODE_STA 1
#define WIFI_MODE_AP 2
#define WIFI_IF_STA 0
#define WIFI_STARTED_BIT 1
struct netif { void (*input)(void); };
typedef struct {struct netif stack;uint8_t mac[6];} esp_netif_t;
typedef struct {bool started;int mode;} esp32_mquickjs_wifi_radio_status_t;
static esp_netif_t netif;
static struct {esp_netif_t *sta_netif;bool started;struct{bool started;}status;int event_group,radio_lease;} s_wifi_state;
static esp32_mquickjs_wifi_radio_status_t observed;
static const char *s_wifi_setup_stage;
static int fail_at,calls,published,lock_depth,fence_error,fenced;
static bool buffers,start_on_fence;
static void tcpip_input(void);
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *out){*out=observed;return 0;}
static int esp32_mquickjs_wifi_radio_ensure_started(int *lease){assert(lease==&s_wifi_state.radio_lease && !lock_depth);fenced++;if(start_on_fence && !fence_error)netif.stack.input=tcpip_input;return fence_error;}
static void wifi_lock(void){assert(!lock_depth);lock_depth++;}
static void wifi_unlock(void){assert(lock_depth);lock_depth--;}
static void xEventGroupSetBits(int group,int bits){(void)group;assert(bits==WIFI_STARTED_BIT);published++;}
static void *esp_netif_get_netif_impl(esp_netif_t *n){return &n->stack;}
static int step(void){assert(fenced==1);return ++calls==fail_at?77:0;}
static int esp_wifi_get_mac(int iface,uint8_t *mac){assert(iface==WIFI_IF_STA && !lock_depth);int e=step();if(!e)memcpy(mac,"\x02\x11\x22\x33\x44\x55",6);return e;}
static void esp_netif_netstack_buf_ref(void *p){(void)p;}
static void esp_netif_netstack_buf_free(void *p){(void)p;}
static int esp_wifi_internal_reg_netstack_buf_cb(void (*ref)(void *),void (*free_fn)(void *)){
    assert(ref==esp_netif_netstack_buf_ref && free_fn==esp_netif_netstack_buf_free && !lock_depth);int e=step();if(!e)buffers=true;return e;
}
static int esp_netif_set_mac(esp_netif_t *n,const uint8_t *mac){assert(buffers && !lock_depth);int e=step();if(!e)memcpy(n->mac,mac,6);return e;}
static void tcpip_input(void){}
static int esp_netif_start(esp_netif_t *n){assert(buffers && !memcmp(n->mac,"\x02\x11\x22\x33\x44\x55",6) && !n->stack.input && !lock_depth);int e=step();if(!e)n->stack.input=tcpip_input;return e;}
static void setup(void){memset(&netif,0,sizeof(netif));memset(&s_wifi_state,0,sizeof(s_wifi_state));s_wifi_state.sta_netif=&netif;calls=published=lock_depth=fenced=fence_error=0;buffers=start_on_fence=false;observed=(esp32_mquickjs_wifi_radio_status_t){true,WIFI_MODE_STA};}
'''
MAIN=r'''
int main(void){
    setup();assert(initialize_suffix()==0);
    assert(netif.stack.input==tcpip_input && calls==4 && published==1 && s_wifi_state.started);
    for(fail_at=1;fail_at<=4;fail_at++){
        setup();assert(initialize_suffix()==77);
        assert(!netif.stack.input && calls==fail_at && !published && !s_wifi_state.started && !s_wifi_state.status.started);
    }
    fail_at=0;setup();netif.stack.input=tcpip_input;
    assert(initialize_suffix()==0 && !calls && published==1 && fenced==1); /* real START already initialized it */
    setup();start_on_fence=true;
    assert(initialize_suffix()==0 && !calls && published==1 && fenced==1); /* real START completes while joining its fence */
    setup();fence_error=78;
    assert(initialize_suffix()==78 && !calls && !published && !netif.stack.input);
    setup();observed.started=false;
    assert(initialize_suffix()==0 && !calls && !published && !netif.stack.input && !fenced);
    setup();observed.mode=WIFI_MODE_AP;
    assert(initialize_suffix()==0 && !calls && !published && !netif.stack.input && !fenced);
    setup();observed.mode=WIFI_MODE_STA|WIFI_MODE_AP;
    assert(initialize_suffix()==0 && calls==4 && published==1 && netif.stack.input);
    return 0;
}
'''
