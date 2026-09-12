"""Production disconnect/IP handoff, with SDK event delivery controlled by tests."""
import unittest
from test_wireless_control_regression import compile_run
import test_wifi_scan_lifecycle as scan_fixture

def record_connection_events(code):
    return code.replace(
        'static void wifi_queue_connect_event(unsigned g,int k,int r,const esp32_mquickjs_wifi_link_snapshot_t *link) { (void)g;(void)k;(void)r;(void)link; }',
        'static int connection_events,last_connection_kind;\nstatic void wifi_queue_connect_event(unsigned g,int k,int r,const esp32_mquickjs_wifi_link_snapshot_t *link) { (void)g;(void)r;(void)link;connection_events++;last_connection_kind=k; }')

class WiFiDisconnectLifecycle(unittest.TestCase):
    def test_late_ip_does_not_complete_disconnect(self):
        code = record_connection_events(scan_fixture.WiFiScanLifecycle().code())
        compile_run(self, code + r'''
int main(void) {
    s_wifi_state.connect_future_registered=true;s_wifi_state.connect_generation=7;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_DISCONNECT;
    const esp32_mquickjs_wifi_driver_event_t ip={.kind=WIFI_DRIVER_EVENT_GOT_IP};
    wifi_process_driver_event(&ip);
    assert(!s_wifi_state.status.connected && !connection_events);
}
''')

    def test_disconnect_return_and_fifo_fence_both_precede_reuse(self):
        code = scan_fixture.WiFiScanLifecycle().code().replace(
            'wifi_process_driver_event(&done);\n    }\n    return disconnect_error;', r'''
        wifi_process_driver_event(&done);
        const esp32_mquickjs_wifi_driver_event_t fence={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
        wifi_process_driver_event(&fence);
        assert(s_wifi_state.connect_draining && s_wifi_state.disconnect_active);
        assert(!(event_bits & WIFI_LINK_DRAINED_BIT));
    }
    return disconnect_error;''')
        compile_run(self, code + r'''
int main(void) {
    s_wifi_state.connect_in_progress=true;complete_disconnect_in_call=true;
    assert(wifi_request_disconnect()==ESP_OK);
    assert(disconnect_calls==1 && posts==1);
    assert(!s_wifi_state.connect_draining && (event_bits & WIFI_LINK_DRAINED_BIT));
}
''')

    def test_queue_full_retries_only_marker_and_rejects_old_epoch(self):
        compile_run(self, record_connection_events(scan_fixture.WiFiScanLifecycle().code()) + r'''
int main(void) {
    s_wifi_state.connect_in_progress=true;post_error=-9;
    s_wifi_state.connect_future_registered=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_DISCONNECT;
    assert(wifi_request_disconnect()==ESP_OK);
    const esp32_mquickjs_wifi_driver_event_t down={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    wifi_process_driver_event(&down);
    assert(s_wifi_state.connect_draining && !s_wifi_state.disconnect_fence_posted);
    assert(s_wifi_state.disconnect_cleanup_error==-9 && disconnect_calls==1 && posts==1);
    assert(connection_events==1 && last_connection_kind==ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_DISCONNECTED);
    post_error=0;assert(wifi_request_disconnect()==ESP_OK);
    assert(disconnect_calls==1 && posts==2 && s_wifi_state.connect_draining);
    const esp32_mquickjs_wifi_driver_event_t old={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch-1};
    wifi_process_driver_event(&old);assert(s_wifi_state.connect_draining);
    const esp32_mquickjs_wifi_driver_event_t fence={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
    wifi_process_driver_event(&fence);assert(!s_wifi_state.connect_draining);
    s_wifi_state.connect_in_progress=true;assert(wifi_request_disconnect()==ESP_OK);
    wifi_process_driver_event(&down);wifi_process_driver_event(&fence);
    assert(s_wifi_state.connect_draining && disconnect_calls==2 && posts==3);
}
''')

    def test_missing_terminal_native_error_and_netif_up_keep_quarantine(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + r'''
int main(void) {
    s_wifi_state.connect_in_progress=true;disconnect_error=-8;
    assert(wifi_request_disconnect()==-8);
    assert(s_wifi_state.connect_draining && s_wifi_state.disconnect_cleanup_error==-8 && !posts);
    disconnect_error=0;assert(wifi_request_disconnect()==ESP_OK);
    assert(wifi_request_disconnect()==ESP_OK && disconnect_calls==2 && !posts);
    netif_up=true;
    const esp32_mquickjs_wifi_driver_event_t down={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    wifi_process_driver_event(&down);
    assert(s_wifi_state.connect_draining && !posts);
    assert(s_wifi_state.disconnect_cleanup_error==ESP_ERR_INVALID_STATE);
    netif_up=false;assert(wifi_request_disconnect()==ESP_OK);
    assert(posts==1 && disconnect_calls==2 && s_wifi_state.connect_draining);
}
''')

    def test_epoch_exhaustion_cannot_be_cleared_by_old_marker(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + r'''
int main(void) {
    s_wifi_state.disconnect_epoch=UINT32_MAX;s_wifi_state.connect_in_progress=true;
    (void)wifi_request_disconnect();
    const esp32_mquickjs_wifi_driver_event_t down={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    const esp32_mquickjs_wifi_driver_event_t old={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=UINT32_MAX};
    wifi_process_driver_event(&down);wifi_process_driver_event(&old);
    assert(s_wifi_state.disconnect_epoch==UINT32_MAX && s_wifi_state.disconnect_epoch_exhausted);
    assert(s_wifi_state.connect_draining && !posts && disconnect_calls==1);
    assert(wifi_request_disconnect()==ESP_ERR_INVALID_STATE);
}
''')

    def test_teardown_preserves_quarantine_and_old_cancel_cannot_touch_new_owner(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + r'''
int main(void) {
    s_wifi_state.initialized=true;s_wifi_state.connect_generation=7;
    s_wifi_state.connect_future_registered=true;s_wifi_state.connect_in_progress=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(!s_wifi_runtime && s_wifi_state.connect_draining && disconnect_calls==1);
    assert(!s_wifi_state.connect_future_registered && s_wifi_state.connect_generation==8);
    const esp32_mquickjs_wifi_driver_event_t ip={.kind=WIFI_DRIVER_EVENT_GOT_IP};
    wifi_process_driver_event(&ip);assert(!s_wifi_state.status.connected);
    s_wifi_state.connect_future_registered=true;s_wifi_state.connect_future_token=9;
    assert(esp32_mquickjs_wifi_cancel_connect(7)==ESP_OK);
    assert(s_wifi_state.connect_future_registered && s_wifi_state.connect_future_token==9 && disconnect_calls==1);
    esp32_mquickjs_future_driver_state_t scan={.kind=WIFI_FUTURE_SCAN};
    assert(!wifi_future_start(NULL,NULL,10,&scan) && !starts);
}
''')

    def test_connect_cannot_adopt_an_unregistered_native_operation(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + r'''
int main(void) {
    s_wifi_state.connect_generation=7;s_wifi_state.connect_in_progress=true;
    esp32_mquickjs_future_driver_state_t connect={.kind=WIFI_FUTURE_CONNECT};
    assert(!wifi_future_start(NULL,NULL,8,&connect));
    assert(!s_wifi_state.connect_future_registered && s_wifi_state.connect_generation==7);
}
''')

    def test_disconnect_after_terminal_does_not_wait_for_a_second_event(self):
        source = scan_fixture.WIFI.read_text()
        code = scan_fixture.WiFiScanLifecycle().code().replace(
            'static int esp32_mquickjs_wifi_start_disconnect(bool *pending) { *pending=false;return 0; }',
            'int esp32_mquickjs_wifi_start_disconnect(bool *pending);')
        code += scan_fixture.extract(source, 'esp32_mquickjs_wifi_start_disconnect')
        compile_run(self, code + r'''
int main(void) {
    s_wifi_state.initialized=true;s_wifi_state.started=true;s_wifi_state.connect_in_progress=true;
    assert(wifi_request_disconnect()==ESP_OK);
    const esp32_mquickjs_wifi_driver_event_t down={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    wifi_process_driver_event(&down);
    esp32_mquickjs_future_driver_state_t disconnect={.kind=WIFI_FUTURE_DISCONNECT};
    assert(wifi_future_start(NULL,NULL,9,&disconnect));
    assert(disconnect.completed && s_wifi_state.connect_draining && disconnect_calls==1);
}
''')

    def test_reconnect_changes_config_only_after_native_handoff(self):
        import test_wifi_connect_timer as timer_fixture
        compile_run(self, record_connection_events(timer_fixture.WiFiConnectTimer().barrier_code()) + r'''
int main(void) {
    s_wifi_state.status.connected=true;s_wifi_state.connect_generation=8;
    s_wifi_state.connect_timeout_timer=(void *)1;s_wifi_state.connect_future_registered=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    wifi_config_t config={.sta.ssid="new-network",.sta.pmf_cfg.capable=true};
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==ESP_ERR_TIMEOUT);
    assert(s_wifi_state.connect_draining && disconnect_calls==1 && !configs && !connects);
    deliver_disconnect_on_wait=true;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==ESP_OK);
    assert(disconnect_calls==1 && configs==1 && connects==1 && !s_wifi_state.connect_draining);
    assert(s_wifi_state.connect_in_progress && !s_wifi_state.status.connected && !connection_events);
    const esp32_mquickjs_wifi_driver_event_t ip={.kind=WIFI_DRIVER_EVENT_GOT_IP};
    wifi_process_driver_event(&ip);
    assert(s_wifi_state.status.connected && !s_wifi_state.connect_in_progress);
    assert(connection_events==1 && last_connection_kind==ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS);
}
''')
