"""Deferred production WPS helper reservation and DHCP/IP event fence cases."""
import re
import unittest
from test_wifi_scan_lifecycle import SDK, WIFI, FUTURE, extract
from test_wireless_control_regression import compile_run


class WPSStation(unittest.TestCase):
    def test_exact_reservation_blocks_futures_until_link_drain(self):
        source = WIFI.read_text()
        record = re.search(r'static struct \{[^}]*\} s_wifi_smartconfig_connect;', source).group(0)
        globals_ = record + '\nstatic uint32_t s_wifi_wps_capture_identity, s_wifi_wps_next_identity=1;\n'
        code = '#define CONFIG_ESP_WIFI_DPP_SUPPORT 1\n#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1\n#define CONFIG_LWIP_IPV4 1\n' + SDK
        marker = 'static _Atomic(esp32_mquickjs_runtime_t *) s_wifi_runtime'
        code = code.replace(marker, globals_ + marker, 1)
        code += BOUNDARIES
        for name in ('wifi_helpers_idle', 'wifi_driver_helpers_ready', 'wifi_begin_disconnect_locked',
                     'wifi_finish_disconnect_locked', 'wifi_post_disconnect_fence', 'wifi_request_disconnect',
                     'esp32_mquickjs_wifi_cancel_connect', 'wifi_process_driver_event'):
            code += extract(source, name)
        code += (WIFI.parent / 'esp32_mquickjs_wifi_wps_station.inc').read_text()
        code += extract(FUTURE.read_text(), 'wifi_future_start')
        compile_run(self, code + CASES)


BOUNDARIES = r'''
#define ESP_ERR_NO_MEM -8
#define ESP_ERR_NOT_FINISHED -9
static struct {unsigned identity;} s_wifi_lifecycle;
esp_err_t esp32_mquickjs_wifi_drain_scan(void) {return ESP_OK;}
static int esp32_mquickjs_wifi_start_scan(const wifi_scan_config_t *c,uint32_t generation) {
    (void)c;(void)generation;assert(0);return ESP_FAIL;
}
'''

CASES = r'''
static void event(int kind,unsigned generation) {
    esp32_mquickjs_wifi_driver_event_t e={.kind=kind,.generation=generation};wifi_process_driver_event(&e);
}
int main(void) {
    s_wifi_state.sta_netif=(void*)1;
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){.identity=2,.generation=5,.acquired=true};
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){.identity=1,.generation=5,.acquired=true};
    uint32_t identity=0;esp32_mquickjs_wifi_radio_lease_t owners[3];
    assert(!esp32_mquickjs_wifi_wps_station_reserve(&identity,owners) && identity==1);
    assert(owners[0].identity==1 && owners[1].identity==2 && !owners[2].acquired);
    assert(s_wifi_state.connect_generation==1 && !s_wifi_state.connect_in_progress);
    assert(!wifi_helpers_idle(true));
    esp32_mquickjs_future_driver_state_t scan={.kind=WIFI_FUTURE_SCAN};
    esp32_mquickjs_future_driver_state_t connect={.kind=WIFI_FUTURE_CONNECT};
    esp32_mquickjs_future_driver_state_t disconnect={.kind=WIFI_FUTURE_DISCONNECT};
    assert(!wifi_future_start(NULL,NULL,1,&scan));
    assert(!wifi_future_start(NULL,NULL,2,&connect));
    assert(!wifi_future_start(NULL,NULL,3,&disconnect));
    assert(esp32_mquickjs_wifi_cancel_connect(1)==ESP_ERR_INVALID_STATE && !disconnect_calls);
    event(WIFI_DRIVER_EVENT_GOT_IP,0);assert(!s_wifi_state.status.connected && !queued && !wakes);
    uint32_t wrong=identity+1;
    assert(esp32_mquickjs_wifi_wps_station_release(&wrong)==ESP_ERR_INVALID_STATE && s_wifi_wps_capture_identity==identity);
    netif_up=true;assert(esp32_mquickjs_wifi_wps_station_drain(identity)==ESP_ERR_NOT_FINISHED);
    netif_up=false;post_error=ESP_FAIL;event(WIFI_DRIVER_EVENT_DISCONNECTED,0);
    assert(s_wifi_state.connect_draining && !s_wifi_state.disconnect_fence_posted);
    assert(esp32_mquickjs_wifi_wps_station_drain(identity)==ESP_FAIL && !disconnect_calls);
    post_error=0;assert(esp32_mquickjs_wifi_wps_station_drain(identity)==ESP_ERR_NOT_FINISHED);
    event(WIFI_DRIVER_EVENT_LINK_DRAINED,fence_epoch+1);
    assert(esp32_mquickjs_wifi_wps_station_drain(identity)==ESP_ERR_NOT_FINISHED);
    event(WIFI_DRIVER_EVENT_LINK_DRAINED,fence_epoch);
    assert(!esp32_mquickjs_wifi_wps_station_drain(identity) && s_wifi_wps_capture_identity==identity);
    uint32_t old=identity;assert(!esp32_mquickjs_wifi_wps_station_release(&identity) && !identity && wifi_helpers_idle(false));
    assert(!esp32_mquickjs_wifi_wps_station_reserve(&identity,owners) && identity!=old && s_wifi_state.connect_generation==2);
    assert(esp32_mquickjs_wifi_wps_station_release(&old)==ESP_ERR_INVALID_STATE && s_wifi_wps_capture_identity==identity);
    assert(!esp32_mquickjs_wifi_wps_station_release(&identity));
    /* DPP shares the slot but a WPS release cannot free its current owner. */
    assert(!esp32_mquickjs_wifi_dpp_station_reserve(&identity,owners));
    old=identity;
    assert(esp32_mquickjs_wifi_wps_station_release(&old)==ESP_ERR_INVALID_STATE);
    assert(s_wifi_wps_capture_identity==identity && !wifi_helpers_idle(false));
    assert(!esp32_mquickjs_wifi_dpp_station_release(&identity));
    assert(esp32_mquickjs_wifi_dpp_station_release(&old)==ESP_ERR_INVALID_STATE);
    s_wifi_wps_next_identity=0;
    assert(esp32_mquickjs_wifi_wps_station_reserve(&identity,owners)==ESP_ERR_NO_MEM && !identity);
    s_wifi_wps_next_identity=3;s_wifi_state.connect_generation=UINT32_MAX;
    assert(esp32_mquickjs_wifi_wps_station_reserve(&identity,owners)==ESP_ERR_NO_MEM && !identity);
    assert(!disconnect_calls);
    return 0;
}
'''
