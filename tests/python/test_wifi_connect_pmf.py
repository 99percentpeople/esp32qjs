"""Production connect preparation; executor boundary verifies handoff only.

Deferred phase fixture. Native stopped PMF transaction, lifecycle admission and
post-START connect readback are covered by their separate production fixtures.
"""
import unittest
from wireless_vm_fixture import ROOT, extract
from test_wifi_config_controls import PRELUDE, sdk_types, structure
from test_wireless_control_regression import compile_run


class WiFiConnectPmf(unittest.TestCase):
    def test_preparation_security_and_exact_executor_request(self):
        base = ROOT / 'components/esp32_mquickjs'
        radio = (base / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        wifi = (base / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        header = (base / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        code = PRELUDE + '#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n' + sdk_types('esp32c5/representative')
        code += structure(header, 'esp32_mquickjs_wifi_radio_configuration_selection_t')
        code += r'''
static int starts,transactions,prepare_error;
static int esp32_mquickjs_wifi_ensure_started(void) { starts++;return prepare_error; }
static bool esp32_mquickjs_wifi_radio_accept_station_config(const wifi_config_t *a,const wifi_config_t *b) {
    return !memcmp(a,b,sizeof(*a));
}
static int wifi_configure_selected_interfaces(
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    wifi_config_t *station,bool (*accept)(const wifi_config_t *,const wifi_config_t *),
    wifi_config_t *ap,void *accept_ap,const void *controls,const void *start_controls,void *execution) {
    assert(!selection->mode_set && !selection->storage_set && !selection->start_only);
    assert(selection->station_set && !selection->access_point_set);
    assert(selection->start_set && selection->start && selection->allow_disconnect);
    assert(station && accept==esp32_mquickjs_wifi_radio_accept_station_config);
    assert(!ap && !accept_ap && !controls && !start_controls && !execution);
    assert(!station->sta.pmf_cfg.capable && !station->sta.pmf_cfg.required);
    transactions++;return prepare_error;
}
'''
        code += extract(radio, 'esp32_mquickjs_wifi_radio_pmf_disable_allowed')
        code += extract(wifi, 'esp32_mquickjs_wifi_prepare_connect')
        code += r'''
int main(void) {
    wifi_config_t config={0};
    assert(esp32_mquickjs_wifi_prepare_connect(NULL)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_prepare_connect(&config)==ESP_ERR_INVALID_ARG);
    config.sta.disable_wpa3_compatible_mode=true;config.sta.pmf_cfg.required=true;
    assert(esp32_mquickjs_wifi_prepare_connect(&config)==ESP_ERR_INVALID_ARG);
    config.sta.pmf_cfg.required=false;config.sta.threshold.authmode=WIFI_AUTH_WPA3_PSK;
    assert(esp32_mquickjs_wifi_prepare_connect(&config)==ESP_ERR_INVALID_ARG);
    assert(!starts && !transactions);
    config.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;prepare_error=-70;
    assert(esp32_mquickjs_wifi_prepare_connect(&config)==-70 && transactions==1 && !starts);
    prepare_error=0;
    assert(esp32_mquickjs_wifi_prepare_connect(&config)==ESP_OK && transactions==2 && !starts);
    config.sta.pmf_cfg.capable=true;
    assert(esp32_mquickjs_wifi_prepare_connect(&config)==ESP_OK && transactions==2 && starts==1);
    prepare_error=-71;
    assert(esp32_mquickjs_wifi_prepare_connect(&config)==-71 && transactions==2 && starts==2);
}
'''
        compile_run(self, code)
