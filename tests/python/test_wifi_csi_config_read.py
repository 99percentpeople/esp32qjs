"""Deferred actual CSI Radio read: exact lease, SDK errors and unavailable targets."""
import unittest
from test_wifi_connection_controls import control_code
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiCsiConfigRead(unittest.TestCase):
    def test_exact_identity_serialization_and_sdk_support(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        for he, profile in ((False, 'esp32c3/representative'), (True, 'esp32c5/representative')):
            code = f'#define CONFIG_SOC_WIFI_HE_SUPPORT {int(he)}\n'
            code += control_code(profile, False, ('wifi_csi_config_t',))
            code += 'static int esp_wifi_get_csi_config(wifi_csi_config_t *p){memset(p,0,sizeof(*p));return sdk_step(false);}\n'
            code += extract(radio, 'esp32_mquickjs_wifi_radio_read_csi_config')
            with self.subTest(profile=profile):
                compile_run(self, code + MAIN)


MAIN = r'''
int main(void){
    reset();esp32_mquickjs_wifi_radio_lease_t lease={7,20,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI,true};
    s_radio.leases[2]=(wifi_radio_live_lease_t){20,lease.client};
    wifi_csi_config_t output;uint32_t generation=99;
    int error=esp32_mquickjs_wifi_radio_read_csi_config(&lease,&output,&generation);
#if CONFIG_SOC_WIFI_HE_SUPPORT
    assert(error==ESP_OK && generation==7 && calls==1);
    fail_at=calls+1;assert(esp32_mquickjs_wifi_radio_read_csi_config(&lease,&output,&generation)==77 && !generation);
#else
    assert(error==ESP_ERR_NOT_SUPPORTED && !generation && !calls);
#endif
    unsigned before=calls;lease.identity++;
    assert(esp32_mquickjs_wifi_radio_read_csi_config(&lease,&output,&generation)==ESP_ERR_INVALID_STATE && calls==before);
    lease.identity--;s_radio.lifecycle.identity=50;
    assert(esp32_mquickjs_wifi_radio_read_csi_config(&lease,&output,&generation)==ESP_ERR_INVALID_STATE && calls==before);
    s_radio.lifecycle.identity=0;s_radio.started=false;
    assert(esp32_mquickjs_wifi_radio_read_csi_config(&lease,&output,&generation)==ESP_ERR_INVALID_STATE && calls==before);
    return 0;
}
'''
