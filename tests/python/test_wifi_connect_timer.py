"""Exercise production Wi-Fi timeout dispatch and Future cancellation."""
import unittest
from test_wireless_control_regression import compile_run
from test_wifi_scan_lifecycle import SDK, WIFI, FUTURE, extract
from wifi_connection_counter_fixture import connection_counter_code

class WiFiConnectTimer(unittest.TestCase):
    def code(self):
        sdk = SDK.replace('int kind,reason;unsigned status;', 'int kind,reason;unsigned status,generation;')
        sdk += '\n#define disconnects disconnect_calls\n'
        source = WIFI.read_text()
        helpers = ''.join(extract(source,n) for n in ['wifi_release_radio_operation','wifi_prepare_radio_operation','esp32_mquickjs_wifi_drain_scan','wifi_start_scan_reserved','esp32_mquickjs_wifi_start_scan','esp32_mquickjs_wifi_cancel_scan','wifi_begin_disconnect_locked','wifi_finish_disconnect_locked','wifi_post_disconnect_fence','wifi_request_disconnect','esp32_mquickjs_wifi_cancel_connect'])
        return sdk + helpers + extract(source,'wifi_process_driver_event') + extract(FUTURE.read_text(),'wifi_future_cancel')

    def test_old_timeout_cannot_disconnect_new_operation(self):
        compile_run(self, self.code() + r'''
int main(void) {
    s_wifi_state.connect_generation=8;s_wifi_state.connect_in_progress=true;
    s_wifi_state.connect_future_registered=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    const esp32_mquickjs_wifi_driver_event_t old={.kind=WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,.generation=7};
    wifi_process_driver_event(&old);
    assert(disconnects==0 && s_wifi_state.connect_in_progress);
    const esp32_mquickjs_wifi_driver_event_t current={.kind=WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,.generation=8};
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_DISCONNECT;
    wifi_process_driver_event(&current);
    assert(disconnects==0 && s_wifi_state.connect_in_progress);
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    wifi_process_driver_event(&current);
    assert(disconnects==1 && !s_wifi_state.connect_in_progress);
}
''')

    def test_prepared_connect_cancel_does_not_disconnect_existing_link(self):
        compile_run(self, self.code() + r'''
int main(void) {
    s_wifi_state.status.connected=true;
    esp32_mquickjs_future_driver_state_t future={.kind=WIFI_FUTURE_CONNECT};
    assert(wifi_future_cancel(&future)==ESP32_MQUICKJS_CANCELLED);
    assert(disconnects==0 && wakes==0 && s_wifi_state.status.connected);
}
''')

    def barrier_code(self):
        sdk = SDK.replace('static void wifi_stop_connect_timeout_timer(void) {}', '')
        sdk = sdk.replace('static int esp32_mquickjs_wifi_prepare_connect_timer(void) { return 0; }', '')
        sdk = sdk.replace('static int esp32_mquickjs_wifi_start_connect(const void *config,unsigned timeout) { (void)config;(void)timeout;return 0; }', '')
        sdk = sdk.replace('    const char *cleanup_stage;', '    void *connect_timeout_timer,*driver_event_queue;\n    const char *cleanup_stage;')
        sdk = sdk.replace('int last_disconnect_reason;esp32_mquickjs_wifi_link_snapshot_t link;', 'int last_disconnect_reason,connect_timer_error;char ssid[33];esp32_mquickjs_wifi_link_snapshot_t link;')
        sdk += '\n#define disconnects disconnect_calls\n'
        sdk += r'''
#define TAG "fixture"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_ERR_WIFI_NOT_CONNECT -4
#define ESP_ERR_TIMEOUT -5
#define ESP_ERR_INVALID_RESPONSE -6
#define WIFI_IF_STA 0
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_RETURN_ON_ERROR(call,...) do { int wifi_test_error=(call);if(wifi_test_error) return wifi_test_error; } while(0)
static _Atomic uint32_t s_wifi_timeout_armed_generation,s_wifi_timeout_pending;
static int timer_stop_error,timer_start_error,connect_error,barriers,configs,connects,arms;
static unsigned counter_lock_depth;
static bool emit_old_timeout;
static bool wifi_publish_driver_event_from_callback(const esp32_mquickjs_wifi_driver_event_t *e);
static size_t strnlen(const char *s,size_t n) { size_t i=0;while(i<n && s[i]) i++;return i; }
static int esp_timer_stop(void *t) { assert(t && !lock_depth);return 0; }
static int esp_timer_stop_blocking(void *t,unsigned ticks) {
    assert(t && ticks && !lock_depth);barriers++;
    assert(atomic_load(&s_wifi_timeout_armed_generation)==0);
    if(emit_old_timeout) {
        const esp32_mquickjs_wifi_driver_event_t old={.kind=WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,.generation=7};
        wifi_publish_driver_event_from_callback(&old);
    }
    return timer_stop_error;
}
static int esp_wifi_set_config(int interface,const wifi_config_t *config) { (void)interface;(void)config;assert(!lock_depth);configs++;return 0; }
static wifi_config_t stored_connect_config;
static int config_reads,config_read_error;
static int esp_wifi_get_config(int interface,wifi_config_t *config) { assert(interface==WIFI_IF_STA && !lock_depth);config_reads++;*config=stored_connect_config;return config_read_error; }
static bool esp32_mquickjs_wifi_radio_accept_station_config(const wifi_config_t *a,const wifi_config_t *b) { return !memcmp(a,b,sizeof(*a)); }
static int esp_wifi_connect(void) { assert(!lock_depth && !counter_lock_depth);connects++;return connect_error; }
static int esp_timer_start_once(void *t,uint64_t us) { assert(t && us && !lock_depth);arms++;return timer_start_error; }
static bool deliver_disconnect_on_wait;
static unsigned wifi_wait_for_bits(unsigned bits,bool clear,unsigned ms,bool *interrupted) {
    (void)bits;(void)clear;(void)ms;assert(!lock_depth);*interrupted=false;
    if(deliver_disconnect_on_wait) {
        const esp32_mquickjs_wifi_driver_event_t down={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
        const esp32_mquickjs_wifi_driver_event_t old_ip={.kind=WIFI_DRIVER_EVENT_GOT_IP};
        wifi_process_driver_event(&down);wifi_process_driver_event(&old_ip);
        const esp32_mquickjs_wifi_driver_event_t fence={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
        wifi_process_driver_event(&fence);
    }
    return event_bits;
}
static int xQueueReceive(void *q,void *event,int wait) { (void)q;(void)event;(void)wait;return 0; }
'''
        source = WIFI.read_text()
        counters = '''
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) do {(void)(p);assert(!counter_lock_depth);++counter_lock_depth;} while(0)
#define portEXIT_CRITICAL(p) do {(void)(p);assert(counter_lock_depth==1);--counter_lock_depth;} while(0)
'''
        return sdk + counters + connection_counter_code() + ''.join(extract(source,n) for n in [
            'wifi_stop_connect_timeout_timer','esp32_mquickjs_wifi_prepare_connect_timer',
            'wifi_publish_driver_event_from_callback','wifi_connect_timeout_cb',
            'wifi_release_radio_operation','wifi_prepare_radio_operation','esp32_mquickjs_wifi_drain_scan','wifi_start_scan_reserved','esp32_mquickjs_wifi_start_scan','esp32_mquickjs_wifi_cancel_scan',
            'wifi_begin_disconnect_locked','wifi_finish_disconnect_locked','wifi_post_disconnect_fence','wifi_request_disconnect','esp32_mquickjs_wifi_cancel_connect',
            'wifi_process_driver_event','wifi_driver_event_poller','wifi_start_connect_reserved','esp32_mquickjs_wifi_start_connect'])

    def test_observations_follow_actual_submission_and_reset_preserves_operation(self):
        compile_run(self, self.barrier_code() + r'''
int main(void) {
    s_wifi_state.connect_timeout_timer=(void *)1;s_wifi_state.connect_generation=8;
    wifi_config_t config={.sta.ssid="fixture",.sta.pmf_cfg.capable=true};
    timer_stop_error=-9;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==-9);
    assert(!s_wifi_connection_counters.attempts && !connects);
    timer_stop_error=0;connect_error=-10;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==-10);
    assert(s_wifi_connection_counters.attempts==1 && s_wifi_connection_counters.submission_failures==1);
    assert(!s_wifi_state.connect_in_progress && !s_wifi_connection_counters.reconnect_attempts);
    wifi_connection_note_association();connect_error=0;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==0);
    assert(s_wifi_connection_counters.attempts==2 && s_wifi_connection_counters.reconnect_attempts==1);
    unsigned armed=atomic_load(&s_wifi_timeout_armed_generation),native_calls=connects;
    esp32_mquickjs_wifi_reset_connection_counters();
    assert(s_wifi_state.connect_in_progress && atomic_load(&s_wifi_timeout_armed_generation)==armed);
    assert(s_wifi_state.connect_generation==8 && connects==native_calls);
    assert(!s_wifi_connection_counters.attempts && s_wifi_connection_counters.ever_associated);
}
''')

    def test_callback_barrier_failure_prevents_config_and_native_connect(self):
        compile_run(self, self.barrier_code() + r'''
int main(void) {
    s_wifi_state.connect_timeout_timer=(void *)1;s_wifi_state.connect_generation=8;
    wifi_config_t config={.sta.ssid="fixture",.sta.pmf_cfg.capable=true};
    atomic_store(&s_wifi_timeout_armed_generation,7);emit_old_timeout=true;timer_stop_error=-9;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==-9);
    assert(configs==0 && connects==0 && arms==0 && barriers==1);
    assert(s_wifi_state.status.connect_timer_error==-9);
    timer_stop_error=0;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==0);
    assert(configs==1 && connects==1 && arms==1 && barriers==2);
    assert(!atomic_load(&s_wifi_timeout_pending));
    assert(atomic_load(&s_wifi_timeout_armed_generation)==8);
    assert(s_wifi_state.status.connect_timer_error==0);
}
''')

    def test_callback_generation_survives_deferred_runtime_poll(self):
        compile_run(self, self.barrier_code() + r'''
int main(void) {
    s_wifi_state.connect_generation=8;s_wifi_state.connect_in_progress=true;
    s_wifi_state.connect_future_registered=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    atomic_store(&s_wifi_timeout_armed_generation,7);
    wifi_connect_timeout_cb(NULL);
    assert(atomic_load(&s_wifi_timeout_pending)==7 && !disconnects);
    wifi_driver_event_poller(NULL,NULL,NULL);
    assert(!disconnects && s_wifi_state.connect_in_progress);
    atomic_store(&s_wifi_timeout_armed_generation,8);
    wifi_connect_timeout_cb(NULL);
    assert(atomic_load(&s_wifi_timeout_pending)==8 && !disconnects);
    wifi_driver_event_poller(NULL,NULL,NULL);
    assert(disconnects==1 && !s_wifi_state.connect_in_progress);
    atomic_store(&s_wifi_timeout_armed_generation,0);
    wifi_connect_timeout_cb(NULL);
    assert(!atomic_load(&s_wifi_timeout_pending));
}
''')

    def test_timer_start_failure_disarms_identity_and_keeps_disconnect_protection(self):
        compile_run(self, self.barrier_code() + r'''
int main(void) {
    s_wifi_state.connect_timeout_timer=(void *)1;s_wifi_state.connect_generation=8;
    wifi_config_t config={.sta.ssid="fixture",.sta.pmf_cfg.capable=true};timer_start_error=-10;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==-10);
    assert(connects==1 && disconnects==1 && !s_wifi_state.connect_in_progress);
    assert(!atomic_load(&s_wifi_timeout_armed_generation));
    assert(s_wifi_state.status.connect_timer_error==-10);
}
''')

    def test_disabled_pmf_verifies_config_without_post_start_setter(self):
        compile_run(self, self.barrier_code() + r'''
int main(void) {
    s_wifi_state.connect_timeout_timer=(void *)1;s_wifi_state.connect_generation=8;
    wifi_config_t config={.sta.ssid="fixture"};
    stored_connect_config=config;stored_connect_config.sta.pmf_cfg.capable=true;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==ESP_ERR_INVALID_RESPONSE);
    assert(config_reads==1 && !configs && !connects && !arms);
    assert(!s_wifi_state.radio_operation.identity);
    stored_connect_config=config;config_read_error=-70;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==-70);
    assert(config_reads==2 && !configs && !connects);
    config_read_error=0;stored_connect_config.sta.password[0]='x';
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==ESP_ERR_INVALID_RESPONSE);
    assert(config_reads==3 && !configs && !connects);
    stored_connect_config=config;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==ESP_OK);
    assert(config_reads==4 && !configs && connects==1 && arms==1);
}
''')
