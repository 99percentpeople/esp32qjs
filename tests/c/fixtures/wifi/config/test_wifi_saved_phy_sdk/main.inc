
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
