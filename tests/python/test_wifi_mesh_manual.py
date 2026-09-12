"""Deferred production Mesh manual scan/parent ownership regressions.

Only parsed as Python AST during API implementation. SDK calls are injectable
boundaries; the real owner, scan helpers and Session job code are included.
"""
import unittest
from test_wifi_mesh_sdk import BASE, source
from test_wifi_mesh_session import source as session_source
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract

HELPERS = r'''
static void open_manual(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();
 assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(4,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));native_self_organized=false;
}
static uint32_t begin_scan(void){
 esp32_mquickjs_wifi_mesh_control_data_t d={.data.scan={.config={.scan_type=WIFI_SCAN_TYPE_PASSIVE,
  .scan_time.passive=360,.home_chan_dwell_time=30,.channel_bitmap.ghz_5_channels=1},.has_ssid=true,.ssid={'a','p'}}};
 esp32_mquickjs_wifi_mesh_control_t c={.kind=ESP32_MQUICKJS_MESH_SCAN,.detail=&d};
 int result=esp32_mquickjs_wifi_mesh_sdk_control(&active,&c);
 assert(result==(scan_fail?ESP_ERR_TIMEOUT:0));return s_mesh->status.scan.identity;
}
static void close_manual(void){assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active)&&!live_allocations);}
'''


class WiFiMeshManual(unittest.TestCase):
    def test_router_station_dhcp_and_disconnect_during_ip_read(self):
        radio = (BASE / 'src/modules/wifi_radio/esp32_mquickjs_wifi_mesh_radio.inc').read_text()
        functions = ''.join(extract(radio, name) for name in (
            'wifi_radio_mesh_dhcp_stop', 'wifi_radio_mesh_ip_parent', 'wifi_radio_mesh_network'))
        compile_run(self, source() + NETWORK + functions + HELPERS + r'''
int main(void){
 open_manual();wifi_radio_mesh_t binding={.sta=&netif,.native=active};
 esp32_mquickjs_wifi_mesh_status_t sample={.token=active,.native_snapshot_valid=true,.parent_known=true,
  .parent_connected=true,.type_known=true,.type=MESH_STA,.events=5};
 esp_netif_ip_info_t ip;bool dhcp,ready;s_mesh->status=sample;
 /* The standalone router station requires DHCP even though it is not root. */
 assert(!wifi_radio_mesh_network(&binding,&sample,&ip,&dhcp,&ready));
 assert(dhcp&&ready&&ip.ip.addr&&dhcp_starts==1);
 sample.type=MESH_NODE;s_mesh->status=sample;
 assert(!wifi_radio_mesh_network(&binding,&sample,&ip,&dhcp,&ready));
 assert(!dhcp&&!ready&&!ip.ip.addr&&dhcp_state==ESP_NETIF_DHCP_STOPPED);
 sample.type=MESH_LEAF;s_mesh->status=sample;
 assert(!wifi_radio_mesh_network(&binding,&sample,&ip,&dhcp,&ready)&&!dhcp&&!ready);
 sample.type=MESH_ROOT;sample.root=true;s_mesh->status=sample;
 assert(!wifi_radio_mesh_network(&binding,&sample,&ip,&dhcp,&ready)&&dhcp&&ready);
 /* A control event between the IP getter and publication must retire readiness. */
 disconnect_on_ip=true;s_mesh->status=sample;
 assert(!wifi_radio_mesh_network(&binding,&sample,&ip,&dhcp,&ready));
 assert(!dhcp&&!ready&&!ip.ip.addr&&dhcp_state==ESP_NETIF_DHCP_STOPPED);
 sample=s_mesh->status;assert(!wifi_radio_mesh_ip_parent(&sample));
 sample.parent_connected=true;sample.native_snapshot_valid=true;sample.closing=true;
 assert(!wifi_radio_mesh_ip_parent(&sample));
 sample.closing=false;sample.root=false;sample.type=MESH_STA;sample.type_known=false;
 assert(!wifi_radio_mesh_ip_parent(&sample));
 s_mesh->status=(esp32_mquickjs_wifi_mesh_status_t){.token=active,.initialized=true,.started=true};
 close_manual();
}
''')

    def test_record_survives_failed_conversion_and_commits_only_exact_identity(self):
        compile_run(self, source() + HELPERS + r'''
int main(void){
 open_manual();uint32_t scan=begin_scan();
 esp32_mquickjs_wifi_mesh_scan_record_t a={0},b={0};
 esp32_mquickjs_wifi_mesh_control_data_t d={.scan_identity=scan,.scan_record=&a};
 esp32_mquickjs_wifi_mesh_control_t c={.kind=ESP32_MQUICKJS_MESH_SCAN_NEXT,.detail=&d};
 assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)&&scan_reads==1&&a.sequence);
 assert(s_mesh->status.scan.remaining==2&&s_mesh->status.scan.retained);
 /* First caller failed conversion: no commit. Next caller gets the same row. */
 d.scan_record=&b;assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&c));
 assert(!memcmp(&a,&b,sizeof(a))&&scan_reads==1);
 assert(esp32_mquickjs_wifi_mesh_sdk_scan_commit(&active,scan+1,a.sequence)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_mesh_sdk_scan_commit(&active,scan,a.sequence+1)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_scan_commit(&active,scan,a.sequence));
 assert(s_mesh->status.scan.remaining==1&&!s_mesh->status.scan.retained);
 assert(esp32_mquickjs_wifi_mesh_sdk_scan_commit(&active,scan,a.sequence)==ESP_ERR_INVALID_STATE);
 c.kind=ESP32_MQUICKJS_MESH_SCAN_FLUSH;assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&c));
 assert(!s_mesh->scan_record&&live_allocations==2);
 uint32_t next=begin_scan();assert(next!=scan);
 c.kind=ESP32_MQUICKJS_MESH_SCAN_NEXT;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_INVALID_STATE&&scan_reads==1);
 close_manual();
}
''')


    def test_oom_bad_ie_length_and_identity_exhaustion_precede_destructive_read(self):
        compile_run(self, source() + HELPERS + r'''
int main(void){
 open_manual();uint32_t scan=begin_scan();esp32_mquickjs_wifi_mesh_scan_record_t a={0};
 esp32_mquickjs_wifi_mesh_control_data_t d={.scan_identity=scan,.scan_record=&a};
 esp32_mquickjs_wifi_mesh_control_t c={.kind=ESP32_MQUICKJS_MESH_SCAN_NEXT,.detail=&d};
 fail_allocation=allocations+1;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_NO_MEM&&!scan_reads&&scan_remaining==2);
 fail_allocation=0;scan_ie_length=ESP32_MQUICKJS_MESH_SCAN_IE_BYTES+1;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_INVALID_SIZE&&!scan_reads&&scan_remaining==2);
 scan_ie_length=4;s_mesh->next_sequence=0;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_NO_MEM&&!scan_reads&&scan_remaining==2);
 close_manual();
}
''')

    def test_ambiguous_native_return_retains_arguments_and_quarantines_lane(self):
        compile_run(self, source() + HELPERS + r'''
int main(void){
 open_manual();scan_fail=true;uint32_t scan=begin_scan();
 assert(s_mesh->status.scan.uncertain&&!s_mesh->status.scan.completed);
 assert(retained_scan_config->ssid==s_mesh->scan_config.ssid&&!memcmp(retained_scan_config->ssid,"ap",3));
 esp32_mquickjs_wifi_mesh_control_data_t d={.scan_identity=scan};
 esp32_mquickjs_wifi_mesh_control_t c={.kind=ESP32_MQUICKJS_MESH_SCAN_FLUSH,.detail=&d};
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_INVALID_STATE);
 c.kind=ESP32_MQUICKJS_MESH_SELF_ORGANIZED;d.enabled=true;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_INVALID_STATE);
 assert(scan_starts==1&&!native_self_organized);
 close_manual();
}
''')

    def test_close_during_blocking_scan_waits_for_worker_return_and_flush_is_verified(self):
        compile_run(self, source() + HELPERS + r'''
int main(void){
 open_manual();scan_close_during_start=true;begin_scan();assert(s_mesh->status.closing&&!deinits);
 close_manual();scan_close_during_start=false;open_manual();uint32_t scan=begin_scan();
 esp32_mquickjs_wifi_mesh_control_data_t d={.scan_identity=scan};
 esp32_mquickjs_wifi_mesh_control_t c={.kind=ESP32_MQUICKJS_MESH_SCAN_FLUSH,.detail=&d};
 scan_ignore_flush=true;assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_INVALID_RESPONSE);
 assert(s_mesh->status.scan.identity==scan);
 scan_ignore_flush=false;assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&c));
 assert(!s_mesh->status.scan.identity);close_manual();
}
''')

    def test_parent_layer_and_manual_admission_precede_native_mode_mutation(self):
        compile_run(self, source() + HELPERS + r'''
int main(void){
 open_manual();esp32_mquickjs_wifi_mesh_control_data_t d={.number=MESH_NODE,.duration=6,
  .data.parent.sta={.ssid={'p'},.channel=6}};
 esp32_mquickjs_wifi_mesh_control_t c={.kind=ESP32_MQUICKJS_MESH_SET_PARENT,.detail=&d};
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_INVALID_ARG&&!parent_sets);
 d.duration=2;d.data.parent.sta.ssid[0]='p';d.data.parent.sta.channel=6;native_self_organized=true;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_INVALID_STATE&&!parent_sets);
 native_self_organized=false;d.data.parent.sta.ssid[0]='p';d.data.parent.sta.channel=6;
 assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)&&parent_sets==1);
 assert(all_zero(&d.data,sizeof(d.data)));close_manual();
}
''')

    def test_scan_reader_identity_and_copied_job_storage_survive_parent_close(self):
        compile_run(self, session_source() + r'''
int main(void){
 test_session=open_session();uint32_t reader=0,other_reader=0;
 assert(!esp32_mquickjs_wifi_mesh_scan_read_claim(test_session,&reader));
 assert(esp32_mquickjs_wifi_mesh_scan_read_claim(test_session,&other_reader)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_mesh_scan_read_release(test_session,reader+1);
 assert(test_session->read_identity[2]==reader);
 esp32_mquickjs_wifi_mesh_control_data_t d={.scan_identity=12};
 esp32_mquickjs_wifi_mesh_control_t c={.kind=ESP32_MQUICKJS_MESH_SCAN_NEXT,.detail=&d};
 esp32_mquickjs_wifi_mesh_job_t*j=NULL;
 assert(!esp32_mquickjs_wifi_mesh_job_create(test_session,NULL,&c,1000,&j));
 assert(j->control.detail->scan_record&&j->control.detail->scan_record!=(void*)&d);
 assert((uintptr_t)j->control.detail->scan_record%_Alignof(esp32_mquickjs_wifi_mesh_scan_record_t)==0);
 esp32_mquickjs_wifi_mesh_session_close(test_session,false);tick();
 assert(esp32_mquickjs_wifi_mesh_scan_commit(test_session,reader,12,1)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_mesh_scan_read_release(test_session,reader);
 esp32_mquickjs_wifi_mesh_session_release(test_session);esp32_mquickjs_wifi_mesh_job_release(j);
 assert(!s_mesh_handles&&!s_mesh_jobs&&!live_allocations);
}
''')


NETWORK = r'''
#define CONFIG_LWIP_DHCPS 0
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED -501
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED -502
typedef struct {int unused;} esp_netif_t;
typedef struct {struct {uint32_t addr;} ip;} esp_netif_ip_info_t;
typedef enum {ESP_NETIF_DHCP_STOPPED,ESP_NETIF_DHCP_STARTED} esp_netif_dhcp_status_t;
typedef struct {esp_netif_t *sta;esp32_mquickjs_wifi_mesh_token_t native;} wifi_radio_mesh_t;
static esp_netif_t netif;
static esp_netif_dhcp_status_t dhcp_state;
static unsigned dhcp_starts;
static bool disconnect_on_ip;
static int esp_netif_dhcpc_stop(esp_netif_t*n){assert(n==&netif);dhcp_state=ESP_NETIF_DHCP_STOPPED;return 0;}
static int esp_netif_dhcpc_start(esp_netif_t*n){assert(n==&netif);++dhcp_starts;dhcp_state=ESP_NETIF_DHCP_STARTED;return 0;}
static int esp_netif_dhcpc_get_status(esp_netif_t*n,esp_netif_dhcp_status_t*s){assert(n==&netif);*s=dhcp_state;return 0;}
static int esp_netif_dhcps_get_status(esp_netif_t*n,esp_netif_dhcp_status_t*s){(void)n;(void)s;assert(0);return 0;}
static int esp_netif_get_ip_info(esp_netif_t*n,esp_netif_ip_info_t*ip){
 assert(n==&netif);ip->ip.addr=0x0100007f;
 if(disconnect_on_ip){s_mesh->status.parent_connected=false;s_mesh->status.native_snapshot_valid=false;++s_mesh->status.events;}
 return 0;
}
static bool esp_netif_is_netif_up(esp_netif_t*n){assert(n==&netif);return true;}
'''
