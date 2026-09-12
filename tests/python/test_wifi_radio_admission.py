"""Production Wi-Fi admission and lifetime; Radio boundary has controlled errors."""
import unittest
import test_wifi_scan_lifecycle as scan_fixture
import test_wifi_connect_timer as timer_fixture
from test_wireless_control_regression import compile_run

class WiFiRadioAdmission(unittest.TestCase):
    def test_scan_conflict_rejected_before_driver_submission(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + r'''
int main(void) {
    admission_error=ESP_ERR_INVALID_STATE;
    esp32_mquickjs_future_driver_state_t scan={.kind=WIFI_FUTURE_SCAN};
    assert(!wifi_future_start(NULL,NULL,1,&scan));
    assert(starts==0 && !s_wifi_state.scan_in_progress && !s_wifi_state.scan_future_registered);
}
''')

    def test_connect_conflict_does_not_disconnect_or_replace_config(self):
        compile_run(self, timer_fixture.WiFiConnectTimer().barrier_code() + r'''
int main(void) {
    admission_error=ESP_ERR_INVALID_STATE;s_wifi_state.status.connected=true;
    s_wifi_state.connect_timeout_timer=(void *)1;
    wifi_config_t config={.sta.ssid="other-network",.sta.pmf_cfg.capable=true};
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==ESP_ERR_INVALID_STATE);
    assert(!disconnect_calls && !configs && !connects && s_wifi_state.status.connected);
}
''')

    def test_scan_reservation_survives_cancel_and_failed_result_cleanup(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t scan={.kind=WIFI_FUTURE_SCAN};
    assert(wifi_future_start(NULL,NULL,1,&scan));
    assert(admissions==1 && s_wifi_state.radio_operation.identity);
    assert(wifi_future_cancel(&scan)==ESP32_MQUICKJS_CANCELLED);
    assert(!operation_releases && s_wifi_state.radio_operation.identity);
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};
    wifi_process_driver_event(&done);clear_error=-9;
    assert(esp32_mquickjs_wifi_drain_scan()==-9 && !operation_releases);
    clear_error=0;assert(esp32_mquickjs_wifi_drain_scan()==0);
    assert(operation_releases==1 && !s_wifi_state.radio_operation.identity);
    assert(esp32_mquickjs_wifi_drain_scan()==0 && operation_releases==1);
}
''')

    def test_scan_submission_failure_releases_reservation(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t scan={.kind=WIFI_FUTURE_SCAN};
    start_error=-8;assert(!wifi_future_start(NULL,NULL,1,&scan));
    assert(admissions==1 && operation_releases==1 && !s_wifi_state.radio_operation.identity);
    start_error=0;assert(wifi_future_start(NULL,NULL,2,&scan));
    assert(admissions==2 && operation_releases==1);
}
''')

    def test_scan_callback_before_submission_return_retains_reservation(self):
        code = scan_fixture.WiFiScanLifecycle().code().replace(
            'starts++;return start_error;', r'''starts++;
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};
    wifi_process_driver_event(&done);
    s_wifi_state.scan_results_pending=false;
    wifi_release_radio_operation();
    assert(!operation_releases && s_wifi_state.radio_operation.identity);
    return start_error;''')
        code = code.replace('static int esp_wifi_scan_start(', 'static void wifi_release_radio_operation(void);\nstatic int esp_wifi_scan_start(')
        compile_run(self, code + r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t scan={.kind=WIFI_FUTURE_SCAN};
    assert(wifi_future_start(NULL,NULL,1,&scan));
    assert(admissions==1 && operation_releases==1 && !s_wifi_state.radio_operation.identity);
}
''')

    def test_connect_timeout_retains_reservation_until_ip_fence(self):
        compile_run(self, timer_fixture.WiFiConnectTimer().barrier_code() + r'''
int main(void) {
    s_wifi_state.connect_generation=8;s_wifi_state.connect_future_registered=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    s_wifi_state.connect_timeout_timer=(void *)1;
    wifi_config_t config={.sta.ssid="network",.sta.pmf_cfg.capable=true};
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==0);
    const esp32_mquickjs_wifi_driver_event_t timeout={.kind=WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,.generation=8};
    wifi_process_driver_event(&timeout);wifi_driver_event_poller(NULL,NULL,NULL);
    assert(admissions==1 && !operation_releases && s_wifi_state.connect_draining);
    const esp32_mquickjs_wifi_driver_event_t down={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    wifi_process_driver_event(&down);wifi_driver_event_poller(NULL,NULL,NULL);
    assert(!operation_releases);
    const esp32_mquickjs_wifi_driver_event_t fence={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
    wifi_process_driver_event(&fence);wifi_driver_event_poller(NULL,NULL,NULL);
    assert(operation_releases==1 && !s_wifi_state.radio_operation.identity);
}
''')

    def test_connect_config_failure_and_success_release_at_native_boundary(self):
        code = timer_fixture.WiFiConnectTimer().barrier_code().replace(
            'configs++;return 0;', 'configs++;return config_error;').replace(
            'static int esp_wifi_set_config(', 'static int config_error;\nstatic int esp_wifi_set_config(')
        from wireless_vm_fixture import extract
        original_connect = extract(code, 'esp_wifi_connect')
        code = code.replace(original_connect, r'''
static void wifi_release_radio_operation(void);
static int esp_wifi_connect(void) {
    assert(!lock_depth && !counter_lock_depth);connects++;
    const esp32_mquickjs_wifi_driver_event_t ip={.kind=WIFI_DRIVER_EVENT_GOT_IP};
    wifi_process_driver_event(&ip);wifi_release_radio_operation();
    assert(operation_releases==1 && s_wifi_state.radio_operation.identity);
    return 0;
}
''')
        compile_run(self, code + r'''
int main(void) {
    s_wifi_state.connect_generation=8;s_wifi_state.connect_future_registered=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    s_wifi_state.connect_timeout_timer=(void *)1;
    wifi_config_t config={.sta.ssid="network",.sta.pmf_cfg.capable=true};
    config_error=-8;assert(esp32_mquickjs_wifi_start_connect(&config,50)==-8);
    assert(admissions==1 && operation_releases==1 && !connects);
    config_error=0;assert(esp32_mquickjs_wifi_start_connect(&config,50)==0);
    assert(s_wifi_state.status.connected && admissions==2 && operation_releases==2);
}
''')

    def test_teardown_keeps_reservation_until_native_handoff_drains(self):
        code = timer_fixture.WiFiConnectTimer().barrier_code()
        code += scan_fixture.extract(scan_fixture.WIFI.read_text(), 'wifi_finish_runtime_cleanup') + scan_fixture.extract(scan_fixture.WIFI.read_text(), 'esp32_mquickjs_deinit_wifi_runtime')
        compile_run(self, code + r'''
int main(void) {
    s_wifi_state.connect_generation=8;s_wifi_state.connect_future_registered=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    s_wifi_state.connect_timeout_timer=(void *)1;
    wifi_config_t config={.sta.ssid="network",.sta.pmf_cfg.capable=true};
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==0);
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(admissions==1 && !operation_releases && s_wifi_state.radio_operation.identity);
    assert(wifi_finish_runtime_cleanup(false)==ESP_ERR_INVALID_STATE);
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    wifi_process_driver_event(&done);
    const esp32_mquickjs_wifi_driver_event_t fence={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
    wifi_process_driver_event(&fence);
    assert(wifi_finish_runtime_cleanup(true)==ESP_OK);
    assert(admissions==1 && operation_releases==1 && connects==1);
    assert(!s_wifi_state.connect_draining && !s_wifi_state.connect_in_progress);
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==0);
    assert(admissions==2 && connects==2);
}
''')

    def test_connect_preparation_precedes_future_registration(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + r'''
int main(void) {
    s_wifi_state.connect_generation=UINT32_MAX;
    esp32_mquickjs_future_driver_state_t state={.kind=WIFI_FUTURE_CONNECT};
    assert(!wifi_future_start(NULL,NULL,1,&state));
    assert(!prepare_connect_calls && !s_wifi_state.connect_future_registered);
    s_wifi_state.connect_generation=8;
    prepare_connect_error=-70;
    assert(!wifi_future_start(NULL,NULL,1,&state));
    assert(prepare_connect_calls==1 && !s_wifi_state.connect_future_registered);
    assert(s_wifi_state.connect_generation==8 && !state.started);
    prepare_connect_error=0;
    assert(wifi_future_start(NULL,NULL,2,&state));
    assert(prepare_connect_calls==2 && s_wifi_state.connect_future_registered);
    assert(s_wifi_state.connect_generation==9 && state.started);
}
''')
