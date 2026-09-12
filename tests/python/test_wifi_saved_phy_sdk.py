"""Deferred saved-PHY production adapter and exact native ioctl dispatch.

Host-width structure access injects the two SDK readers. C5 layout assertions,
archive audit and target compilation are separate from these decision tests.
No fixture is imported, compiled or run during API implementation.
"""
import re
import unittest
from test_wifi_action_lane import PRELUDE
from test_wifi_action_sdk import BOUNDARIES as ACTION_BOUNDARIES
from test_wifi_config_controls import sdk_types, structure
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiSavedPhySdk(unittest.TestCase):
    def test_saved_both_bands_output_commit_and_private_dispatch(self):
        source = (COMPONENT / 'src/modules/wifi_action/esp32_mquickjs_wifi_action_sdk.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_action_sdk.h').read_text()
        for ap in (0, 1):
            code = PRELUDE + f'\n#define CONFIG_IDF_TARGET_ESP32C5 1\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {ap}\n#define ESP_ERR_NOT_SUPPORTED -4\n'
            code += sdk_types('esp32c5/representative', ('wifi_action_tx_req_t', 'wifi_roc_req_t', 'wifi_protocols_t', 'wifi_bandwidths_t'))
            code += structure(header, 'esp32_mquickjs_wifi_saved_phy_t')
            code += structure(source, 'action_sdk_phy_message_t') + structure(source, 'action_sdk_phy_request_t')
            code += '#define ACTION_SDK_SAVED_PHY_TYPE ((wifi_action_tx_t)(INT32_MAX - 2))\n'
            code += ACTION_BOUNDARIES + BOUNDARIES
            for name in ('esp32_mquickjs_wifi_action_receive', 'action_sdk_owner', 'action_sdk_guarded_callback', 'action_sdk_saved_phy_process',
                         '__wrap_wifi_action_tx_process', 'esp32_mquickjs_wifi_action_sdk_saved_phy'):
                code += extract(source, name)
            compile_run(self, code + MAIN)


BOUNDARIES = r'''
static unsigned queries,protocol_reads,width_reads;
static int protocol_error,width_error;
static esp_err_t __wrap_wifi_action_tx_process(void *);
static int wifi_get_protocols_process(void *opaque) {
    action_sdk_phy_message_t *message=opaque;
    assert(ioctl_task && message->interface<=1 && message->ghz_2g && message->ghz_5g);
    ++protocol_reads;*(uint16_t *)message->ghz_2g=7;*(uint16_t *)message->ghz_5g=20;
    return protocol_error;
}
static int wifi_get_bw_process(void *opaque) {
    action_sdk_phy_message_t *message=opaque;
    assert(ioctl_task && message->interface<=1 && message->ghz_2g && message->ghz_5g);
    ++width_reads;*(wifi_bandwidth_t *)message->ghz_2g=WIFI_BW40;*(wifi_bandwidth_t *)message->ghz_5g=WIFI_BW20;
    return width_error;
}
static int esp_wifi_action_tx_req(wifi_action_tx_req_t *request) {
    uint8_t message[32]={0};memcpy(message+20,&request,sizeof(request));
    assert(!ioctl_task);++queries;ioctl_task=true;
    int error=__wrap_wifi_action_tx_process(message);ioctl_task=false;return error;
}
'''

MAIN = r'''
int main(void) {
    esp32_mquickjs_wifi_saved_phy_t result,untouched;memset(&untouched,0xa5,sizeof(untouched));result=untouched;
    assert(esp32_mquickjs_wifi_action_sdk_saved_phy(WIFI_IF_STA,NULL)==ESP_ERR_INVALID_ARG && !queries);
    assert(esp32_mquickjs_wifi_action_sdk_saved_phy((wifi_interface_t)9,&result)==ESP_ERR_INVALID_ARG && !queries);
    for(unsigned interface=0;interface<2;++interface) {
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        if(interface==1) {
            unsigned before=queries;
            assert(esp32_mquickjs_wifi_action_sdk_saved_phy(WIFI_IF_AP,&result)==ESP_ERR_NOT_SUPPORTED && queries==before);
            continue;
        }
#endif
        protocol_reads=width_reads=0;protocol_error=71;width_error=0;result=untouched;
        assert(esp32_mquickjs_wifi_action_sdk_saved_phy(interface,&result)==71);
        assert(protocol_reads==1 && !width_reads && !memcmp(&result,&untouched,sizeof(result)));
        protocol_error=0;width_error=72;
        assert(esp32_mquickjs_wifi_action_sdk_saved_phy(interface,&result)==72);
        assert(width_reads==1 && !memcmp(&result,&untouched,sizeof(result)));
        width_error=0;
        assert(esp32_mquickjs_wifi_action_sdk_saved_phy(interface,&result)==0);
        assert(result.protocols.ghz_2g==7 && result.protocols.ghz_5g==20);
        assert(result.bandwidths.ghz_2g==WIFI_BW40 && result.bandwidths.ghz_5g==WIFI_BW20 && !delegates);
    }
    /* Another module's callback never enters the private reader. */
    action_sdk_phy_request_t foreign={.request={.ifx=WIFI_IF_STA,.type=ACTION_SDK_SAVED_PHY_TYPE,.rx_cb=other_receive}};
    unsigned reads=protocol_reads;
    assert(esp_wifi_action_tx_req(&foreign.request)==77 && delegates==1 && protocol_reads==reads);
    assert(!ioctl_task);return 0;
}
'''
