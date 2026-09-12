"""Production shared AP helper and coordinator handoff; phase execution pending."""
import unittest
import test_wifi_ap_stop as fixture
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiAPReopen(unittest.TestCase):
    def code(self):
        code=fixture.WiFiAPStop().code()+BOUNDARIES
        code+=extract(fixture.fixture.AP.read_text(),'esp32_mquickjs_wifi_ap_reopen')
        code+=extract(fixture.fixture.WIFI.read_text(),'esp32_mquickjs_wifi_reopen_ap_shared')
        return code

    def test_setup_and_activation_failures_keep_the_same_token_for_stop_ap(self):
        compile_run(self,self.code()+r'''
int main(void) {
    for(reopen_failure=0;reopen_failure<=3;reopen_failure++) {
        setup();s_ap_netif=NULL;memset(&s_ap_lease,0,sizeof(s_ap_lease));
        reopen_begins=prepares=reopen_finishes=aborts=0;
        wifi_config_t config={.ap.channel=1};bool handled=false;
        int err=esp32_mquickjs_wifi_reopen_ap_shared(&config,&handled);
        assert(handled && s_wifi_application.identity==1 && s_wifi_state.radio_lease.identity==2);
        assert(s_wifi_state.status.connected && sta_storage && !sta_calls && !stop_calls && !shutdown_calls);
        if(!reopen_failure) {
            assert(!err && config.ap.channel==6 && s_ap_lease.acquired && s_ap_netif);
            assert(!s_wifi_lifecycle.identity && !s_ap_coordinator.identity && !s_ap_cleanup_pending && !s_wifi_ap_stop_cleanup);
            assert(reopen_finishes==1 && !aborts);
            /* Reset only fixture AP state before the next independent case. */
            s_ap_netif=NULL;memset(&s_ap_lease,0,sizeof(s_ap_lease));
        } else if(reopen_failure==1) {
            assert(err==-11 && !prepares && !reopen_finishes && !aborts && !s_ap_lease.acquired);
            assert(!s_ap_cleanup_pending && !s_wifi_ap_stop_cleanup && !s_wifi_lifecycle.identity);
        } else {
            assert(err==-10-reopen_failure && aborts==1 && s_wifi_ap_stop_cleanup && s_ap_partial_stop);
            assert(s_wifi_lifecycle.identity==41 && s_ap_coordinator.identity==41 && s_ap_lease.acquired);
            assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==123);
            assert(!s_wifi_lifecycle.identity && !s_ap_coordinator.identity && !s_ap_netif && !s_ap_lease.acquired);
            assert(s_wifi_state.status.connected && sta_storage && !sta_calls && !stop_calls);
        }
    }
}
''')

    def test_pending_station_operation_never_begins_shared_activation(self):
        compile_run(self,self.code()+r'''
int main(void) {
    setup();s_ap_netif=NULL;memset(&s_ap_lease,0,sizeof(s_ap_lease));
    s_wifi_state.scan_future_registered=true;
    wifi_config_t config={0};bool handled=false;
    assert(esp32_mquickjs_wifi_reopen_ap_shared(&config,&handled)==ESP_ERR_INVALID_STATE && handled);
    assert(!reopen_begins && !s_wifi_lifecycle.identity && !s_wifi_ap_stop_cleanup);
    s_wifi_state.scan_future_registered=false;s_wifi_state.connect_draining=true;
    assert(esp32_mquickjs_wifi_reopen_ap_shared(&config,&handled)==ESP_ERR_INVALID_STATE);
    assert(!reopen_begins && !s_ap_cleanup_pending && s_wifi_state.status.connected);
}
''')


BOUNDARIES = r'''
typedef struct { struct { uint8_t channel; } ap; } wifi_config_t;
static int reopen_failure,reopen_begins,prepares,reopen_finishes,aborts;
static int esp32_mquickjs_wifi_radio_begin_ap_reopen(
    const esp32_mquickjs_wifi_radio_lease_t *app,const esp32_mquickjs_wifi_radio_lease_t *sta,
    const wifi_config_t *config,esp32_mquickjs_wifi_radio_lease_t *ap,
    esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(config && !s_ap_netif && !ap->acquired);reopen_begins++;
    if(reopen_failure==1)return -11;
    int err=esp32_mquickjs_wifi_radio_begin_lifecycle(app,sta,ap,token);
    if(!err)*ap=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};return err;
}
static int esp32_mquickjs_wifi_radio_check_ap_reopen(const esp32_mquickjs_wifi_radio_lifecycle_t *t) { assert(exact(t));return 0; }
static int wifi_ap_prepare_netif(void) { assert(s_ap_coordinator.identity==41 && !s_ap_netif);prepares++;s_ap_netif=(void *)1;return reopen_failure==2?-12:0; }
static int esp32_mquickjs_wifi_radio_finish_ap_reopen(esp32_mquickjs_wifi_radio_lifecycle_t *t,uint8_t *primary) {
    assert(exact(t) && s_ap_netif);reopen_finishes++;
    if(reopen_failure==3)return -13;
    *primary=6;memset(t,0,sizeof(*t));memset(&native_token,0,sizeof(native_token));return 0;
}
static int esp32_mquickjs_wifi_radio_abort_ap_reopen(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    assert(exact(t));aborts++;partial_quiesced=true;return 0;
}
'''
