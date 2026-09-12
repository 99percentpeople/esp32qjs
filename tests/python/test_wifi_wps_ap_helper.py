"""Deferred production AP helper reservation; AST only this wave."""
import unittest
from pathlib import Path
from test_wireless_control_regression import compile_run, function

ROOT = Path(__file__).resolve().parents[2]
WIFI = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c'


class WpsAPHelper(unittest.TestCase):
    def test_exact_reservation_preserves_connected_station_and_blocks_competing_helpers(self):
        source = WIFI.read_text()
        code = TYPES
        for name in ('esp32_mquickjs_wifi_connection_reserved_locked', 'wifi_helpers_idle', 'wifi_driver_helpers_ready'):
            code += function(source, name)
        code += (WIFI.parent / 'esp32_mquickjs_wifi_wps_ap_helper.inc').read_text()
        compile_run(self, code + r'''
int main(void){
 s_wifi_state.status.connected=true;s_wifi_state.connect_generation=33;
 uint32_t id=0;esp32_mquickjs_wifi_radio_lease_t owners[3];
 assert(esp32_mquickjs_wifi_wps_ap_helper_reserve(&id,owners)==0 && id==1);
 assert(owners[0].identity==1 && owners[1].identity==2 && owners[2].identity==3);
 assert(s_wifi_state.status.connected && s_wifi_state.connect_generation==33);
 assert(esp32_mquickjs_wifi_wps_ap_helper_held() && !wifi_helpers_idle(true));
 assert(esp32_mquickjs_wifi_connection_reserved_locked());
 uint32_t wrong=id+1;assert(esp32_mquickjs_wifi_wps_ap_helper_release(&wrong)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_wps_ap_helper_held());
 uint32_t old=id;assert(esp32_mquickjs_wifi_wps_ap_helper_release(&id)==0 && !id && wifi_helpers_idle(true));
 assert(s_wifi_state.status.connected && s_wifi_state.connect_generation==33);
 assert(esp32_mquickjs_wifi_wps_ap_helper_reserve(&id,owners)==0 && id>old);
 assert(esp32_mquickjs_wifi_wps_ap_helper_release(&old)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_wps_ap_helper_release(&id)==0);
 s_wifi_wps_capture_identity=7;
 assert(esp32_mquickjs_wifi_wps_ap_helper_reserve(&id,owners)==ESP_ERR_INVALID_STATE && !id);
 s_wifi_wps_capture_identity=0;ap.acquired=false;
 assert(esp32_mquickjs_wifi_wps_ap_helper_reserve(&id,owners)==ESP_ERR_INVALID_STATE);
 ap.acquired=true;s_wifi_state.connect_draining=true;
 assert(esp32_mquickjs_wifi_wps_ap_helper_reserve(&id,owners)==ESP_ERR_INVALID_STATE);
 s_wifi_state.connect_draining=false;s_wifi_wps_ap_next_helper_identity=0;
 assert(esp32_mquickjs_wifi_wps_ap_helper_reserve(&id,owners)==ESP_ERR_NO_MEM && !id);
 return 0;
}
''')


TYPES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
typedef int esp_err_t;
typedef struct{uint32_t identity;bool acquired;} esp32_mquickjs_wifi_radio_lease_t;
static esp32_mquickjs_wifi_radio_lease_t s_wifi_application={1,true},ap={3,true};
static struct {
 struct{bool connected;}status;
 bool connect_in_progress,connect_draining,connect_start_active,disconnect_active;
 bool scan_in_progress,scan_draining,scan_results_pending,scan_start_active,scan_stop_active;
 bool scan_future_registered,connect_future_registered,runtime_cleanup_pending;
 const char *cleanup_stage;
 unsigned connect_generation;
 esp32_mquickjs_wifi_radio_lease_t radio_lease;
}s_wifi_state={.radio_lease={2,true}};
static struct{struct{uint32_t identity;}owner;}s_wifi_smartconfig_connect;
static struct{uint32_t identity;}s_wifi_lifecycle;
static bool s_wifi_configuration_cleanup,s_wifi_ap_stop_cleanup,locked;
static uint32_t s_wifi_wps_capture_identity,s_wifi_wps_ap_helper_identity,s_wifi_wps_ap_next_helper_identity=1;
static void wifi_lock(void){assert(!locked);locked=true;}
static void wifi_unlock(void){assert(locked);locked=false;}
static const esp32_mquickjs_wifi_radio_lease_t *esp32_mquickjs_wifi_ap_control_lease(void){return ap.acquired?&ap:0;}
'''
