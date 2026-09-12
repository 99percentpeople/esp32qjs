"""Deferred production Station handoff, terminal storage and disconnect fences.

Uses the real SmartConfig connection hooks and existing Station event/disconnect
helpers. Radio admission, SDK submit and timer stop are injected boundaries;
this does not prove native SDK scheduling or RF behavior. Do not run until the
concentrated Wi-Fi validation phase.
"""
import re
import unittest
from test_wifi_scan_lifecycle import SDK, WIFI, FUTURE, extract
from test_wireless_control_regression import compile_run


def connection_code(defines='', boundaries='', functions=()):
    """Shared production connection/event paths; only SDK/Radio edges vary."""
    source = WIFI.read_text()
    record = re.search(r'static struct \{[^}]*\} s_wifi_smartconfig_connect;', source).group(0)
    code = defines + '#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1\n#define CONFIG_LWIP_IPV4 1\n' + SDK
    marker = 'static _Atomic(esp32_mquickjs_runtime_t *) s_wifi_runtime'
    code = code.replace(marker, record + '\nstatic uint32_t s_wifi_wps_capture_identity;\n' + marker, 1)
    code = re.sub(r'static void wifi_queue_connect_event\([^\n]*\n', '', code)
    code = code.replace('const esp32_mquickjs_wifi_scan_event_t *e) { (void)q;queued++;last_generation=e->generation;',
                        'const void *event) { (void)q;const esp32_mquickjs_wifi_scan_event_t *e=event;queued++;last_generation=e->generation;')
    code += BOUNDARIES + boundaries
    for name in ('wifi_queue_connect_event', 'wifi_helpers_idle', 'wifi_driver_helpers_ready',
                 'wifi_smartconfig_connection_exact_locked',
                 'wifi_begin_disconnect_locked', 'wifi_finish_disconnect_locked',
                 'wifi_post_disconnect_fence', 'wifi_request_disconnect',
                 'esp32_mquickjs_wifi_cancel_connect', 'wifi_process_driver_event',
                 'wifi_station_connect_submit', 'esp32_mquickjs_wifi_smartconfig_connect_begin',
                 'esp32_mquickjs_wifi_smartconfig_connect_status',
                 'esp32_mquickjs_wifi_smartconfig_connect_end') + functions:
        code += extract(source, name)
    return code + extract(FUTURE.read_text(), 'wifi_future_start')


class SmartConfigConnection(unittest.TestCase):
    def test_exact_generation_completion_and_cleanup_without_future_token(self):
        compile_run(self, connection_code() + MAIN)


BOUNDARIES = r'''
#define ESP_ERR_NO_MEM -8
#define ESP_ERR_NOT_FINISHED -9
static struct {unsigned identity;} s_wifi_lifecycle;
typedef struct {
 uint32_t generation,terminal;int32_t reason;bool connected,draining;
} esp32_mquickjs_wifi_smartconfig_connection_status_t;
typedef struct {
 uint32_t generation,kind;int32_t reason;int64_t completed_us;
 esp32_mquickjs_wifi_link_snapshot_t link;
} esp32_mquickjs_wifi_connect_event_t;
static esp32_mquickjs_wifi_radio_operation_t borrowed;
static unsigned borrow_calls,release_calls,submit_calls;
static int borrow_error,submit_error,release_error;
static esp_err_t esp32_mquickjs_wifi_radio_smartconfig_connection_begin(const esp32_mquickjs_wifi_radio_operation_t *owner){
 assert(!lock_depth && !borrowed.identity);borrow_calls++;
 if(borrow_error)return borrow_error;borrowed=*owner;return 0;
}
static esp_err_t esp32_mquickjs_wifi_radio_smartconfig_connection_end(const esp32_mquickjs_wifi_radio_operation_t *owner){
 assert(!lock_depth && !memcmp(&borrowed,owner,sizeof(*owner)));release_calls++;
 if(release_error)return release_error;memset(&borrowed,0,sizeof(borrowed));return 0;
}
static esp_err_t wifi_start_connect_reserved(const wifi_config_t *config,uint32_t timeout){
 assert(!lock_depth && borrowed.identity && config->sta.pmf_cfg.capable && timeout);
 submit_calls++;if(!submit_error)s_wifi_state.connect_in_progress=true;return submit_error;
}
/* Unrelated scan is idle; submitting one is unavailable in this fixture. */
esp_err_t esp32_mquickjs_wifi_drain_scan(void){assert(!s_wifi_state.scan_in_progress && !s_wifi_state.scan_draining);return ESP_OK;}
static int esp32_mquickjs_wifi_start_scan(const wifi_scan_config_t *c,uint32_t generation){(void)c;(void)generation;assert(0);return ESP_FAIL;}
'''

MAIN = r'''
static const esp32_mquickjs_wifi_radio_operation_t owner={.identity=19,.generation=5,.lease_identity=2,.kind=3};
static const wifi_config_t config={.sta.ssid="fixture",.sta.pmf_cfg.capable=true};
static void event(int kind,unsigned generation){
 esp32_mquickjs_wifi_driver_event_t e={.kind=kind,.generation=generation};wifi_process_driver_event(&e);
}
static void reset(void){
 assert(!borrowed.identity && !s_wifi_smartconfig_connect.owner.identity && !lock_depth);
 memset(&s_wifi_state,0,sizeof(s_wifi_state));s_wifi_state.sta_netif=(void *)1;
 borrow_error=submit_error=release_error=post_error=disconnect_error=0;
 queued=wakes=disconnect_calls=posts=borrow_calls=release_calls=submit_calls=0;
}
int main(void){
 reset();uint32_t generation=0;
 assert(!esp32_mquickjs_wifi_smartconfig_connect_begin(&owner,&config,1000,&generation));
 assert(generation==1 && !s_wifi_state.connect_future_registered && borrowed.identity);
 esp32_mquickjs_wifi_smartconfig_connection_status_t status;
 esp32_mquickjs_future_driver_state_t scan={.kind=WIFI_FUTURE_SCAN};
 assert(!wifi_future_start(NULL,NULL,1,&scan));
 assert(esp32_mquickjs_wifi_cancel_connect(generation)==ESP_ERR_INVALID_STATE && !disconnect_calls);
 wifi_queue_connect_event(generation+1,2,77,NULL);
 assert(!esp32_mquickjs_wifi_smartconfig_connect_status(&owner,generation,&status) && !status.terminal);
 event(WIFI_DRIVER_EVENT_GOT_IP,0);
 assert(!esp32_mquickjs_wifi_smartconfig_connect_status(&owner,generation,&status) && status.terminal==1 && status.connected);
 assert(!queued && !wakes); /* A native result exists without a queue or Future. */
 wifi_queue_connect_event(generation,2,88,NULL);
 assert(!esp32_mquickjs_wifi_smartconfig_connect_status(&owner,generation,&status) && status.terminal==1);
 esp32_mquickjs_wifi_radio_operation_t stale=owner;stale.lease_identity++;
 assert(esp32_mquickjs_wifi_smartconfig_connect_end(&stale,generation,false)==ESP_ERR_INVALID_STATE && !disconnect_calls);
 release_error=ESP_FAIL;
 assert(esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,true)==ESP_FAIL && borrowed.identity);
 release_error=0;assert(!esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,true));
 assert(s_wifi_state.status.connected && !disconnect_calls && !borrowed.identity);

 reset();generation=0;assert(!esp32_mquickjs_wifi_smartconfig_connect_begin(&owner,&config,1000,&generation));
 event(WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,generation+1);assert(!disconnect_calls);
 event(WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,generation);assert(disconnect_calls==1);
 assert(!esp32_mquickjs_wifi_smartconfig_connect_status(&owner,generation,&status) && status.terminal==3);
 assert(esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,false)==ESP_ERR_NOT_FINISHED && !release_calls);
 post_error=ESP_FAIL;event(WIFI_DRIVER_EVENT_DISCONNECTED,0);
 assert(esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,false)==ESP_FAIL && !release_calls);
 post_error=0;assert(esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,false)==ESP_ERR_NOT_FINISHED);
 event(WIFI_DRIVER_EVENT_LINK_DRAINED,fence_epoch+1);assert(s_wifi_state.connect_draining);
 event(WIFI_DRIVER_EVENT_LINK_DRAINED,fence_epoch);
 assert(!esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,false) && release_calls==1);
 assert(disconnect_calls==1 && !s_wifi_smartconfig_connect.owner.identity);

 reset();generation=0;submit_error=ESP_FAIL;
 assert(esp32_mquickjs_wifi_smartconfig_connect_begin(&owner,&config,1000,&generation)==ESP_FAIL && generation && borrowed.identity);
 assert(!esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,false) && !disconnect_calls);
 reset();generation=0;s_wifi_state.connect_generation=UINT32_MAX;
 assert(esp32_mquickjs_wifi_smartconfig_connect_begin(&owner,&config,1000,&generation)==ESP_ERR_NO_MEM && !borrow_calls && !generation);
 reset();generation=0;s_wifi_state.connect_draining=true;
 assert(esp32_mquickjs_wifi_smartconfig_connect_begin(&owner,&config,1000,&generation)==ESP_ERR_NOT_FINISHED && !borrow_calls);
 return 0;
}
'''
