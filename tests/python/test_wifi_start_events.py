"""A cached Station START cannot bypass production Radio admission/fault checks."""
import pathlib
import unittest
from wireless_vm_fixture import extract
from test_wireless_control_regression import compile_run

ROOT = pathlib.Path(__file__).resolve().parents[2]


class WiFiStartEvents(unittest.TestCase):
    def test_cached_started_still_checks_exact_radio_owner_and_native_result(self):
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        compile_run(self, r'''
#include <assert.h>
#include <stdbool.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -1
#define WIFI_MODE_STA 1
#define WIFI_STARTED_BIT 4
#define ESP_RETURN_ON_ERROR(expr,...) do { int code=(expr);if(code)return code; } while(0)
typedef int esp_err_t;
typedef struct { bool started;unsigned mode; } esp32_mquickjs_wifi_radio_status_t;
static struct { bool started;struct { bool started; } status;int radio_lease,event_group; } s_wifi_state;
static int init_error,start_error,status_error,start_calls,status_calls,bits;
static esp32_mquickjs_wifi_radio_status_t observed={true,WIFI_MODE_STA};
static int wifi_init_once(void) { return init_error; }
static int esp32_mquickjs_wifi_radio_ensure_started(int *lease) {
    assert(lease==&s_wifi_state.radio_lease);start_calls++;return start_error;
}
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *status) {
    status_calls++;*status=observed;return status_error;
}
static void wifi_lock(void) {}
static void wifi_unlock(void) {}
static void xEventGroupSetBits(int group,unsigned value) { (void)group;bits|=value; }
''' + extract(source, 'esp32_mquickjs_wifi_ensure_started') + r'''
int main(void) {
    s_wifi_state.started=s_wifi_state.status.started=true;
    start_error=-11;
    assert(esp32_mquickjs_wifi_ensure_started()==-11 && start_calls==1 && !status_calls && !bits);
    start_error=0;status_error=-12;
    assert(esp32_mquickjs_wifi_ensure_started()==-12 && start_calls==2 && status_calls==1 && !bits);
    status_error=0;observed.mode=2;
    assert(esp32_mquickjs_wifi_ensure_started()==ESP_ERR_INVALID_STATE && !bits);
    observed.mode=WIFI_MODE_STA;observed.started=false;
    assert(esp32_mquickjs_wifi_ensure_started()==ESP_ERR_INVALID_STATE && !bits);
    observed.started=true;
    assert(esp32_mquickjs_wifi_ensure_started()==ESP_OK && bits==WIFI_STARTED_BIT);
    init_error=-13;int starts=start_calls;
    assert(esp32_mquickjs_wifi_ensure_started()==-13 && start_calls==starts);
    return 0;
}
''')
