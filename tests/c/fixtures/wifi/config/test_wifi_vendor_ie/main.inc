
int main(void) {
    reset_vendor();s_radio.driver_owned=false;
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK && !native_calls);
    assert(put(1,0)==ESP_ERR_WIFI_NOT_INIT && !owners());
    reset_vendor();on_lock=late_lifecycle;assert(put(1,0)==ESP_ERR_INVALID_STATE && !native_calls && !owners());
    reset_vendor();s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(put(1,0)==ESP_OK && owners()==1);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK && !owners());
    reset_vendor();s_radio.next_lease_identity=0;assert(put(1,0)==ESP_ERR_NO_MEM && !native_calls);
    reset_vendor();for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)s_radio.leases[i].identity=i+100;
    assert(put(1,0)==ESP_ERR_NO_MEM && !native_calls);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    reset_vendor();esp32_mquickjs_wifi_radio_lease_t ap={0};
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&ap)==ESP_OK);
    wifi_radio_operation_unlock();
    assert(put(0,0)==ESP_OK && put(1,0)==ESP_OK && owners()==2);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK && !owners());
    wifi_radio_operation_lock();assert(wifi_radio_lease_valid(&ap));wifi_radio_release_locked(&ap);wifi_radio_operation_unlock();
#endif
    reset_vendor();assert(put(1,0)==ESP_OK && owners()==1 && invalidations==1);
    assert(lifecycle()==ESP_ERR_INVALID_STATE);unsigned before=native_calls;
    assert(put(1,0)==ESP_ERR_INVALID_STATE && native_calls==before);
    assert(put(3,1)==ESP_OK && owners()==1);
    fail_call=native_calls+1;assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==77 && owners()==1);
    assert(removal_calls[2]==1 && removal_calls[7]==1 && s_vendor_ie.slots[2].pending && !s_vendor_ie.slots[7].length);
    fail_call=0;assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK && !owners());
    assert(removal_calls[2]==2 && removal_calls[7]==1 && lifecycle()==ESP_OK);
    reset_vendor();fail_call=1;assert(put(1,0)==77 && owners()==1 && s_vendor_ie.slots[2].pending);
    assert(lifecycle()==ESP_ERR_INVALID_STATE);fail_call=0;
    assert(put(1,0)==ESP_ERR_INVALID_STATE && native_calls==1);
    s_radio.fault_stage="unrelated";s_radio.fault_error=88;
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK && !owners() && s_radio.fault_error==88);
    reset_vendor();assert(put(1,0)==ESP_OK);esp32_mquickjs_wifi_radio_lease_t old=s_vendor_ie.leases[0];
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK);assert(put(1,0)==ESP_OK);
    wifi_radio_operation_lock();wifi_radio_release_locked(&old);wifi_radio_operation_unlock();assert(owners()==1);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK);
    reset_vendor();
    for(unsigned frame=0;frame<5;++frame) for(unsigned index=0;index<2;++index) {
        int expected=!CONFIG_ESP_WIFI_SOFTAP_SUPPORT && wifi_radio_vendor_ie_interface((wifi_vendor_ie_type_t)frame)==WIFI_IF_AP ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
        assert(put(frame,index)==expected);
    }
    assert(owners()==(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 2 : 1));
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(WIFI_IF_STA)==ESP_OK);
    assert(owners()==(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 1 : 0));
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK && !owners());
    reset_vendor();uint8_t maximum[257]={221,255,1,2,3,4};
    assert(esp32_mquickjs_wifi_radio_vendor_ie_set(WIFI_IF_STA,WIFI_VND_IE_TYPE_PROBE_REQ,0,true,maximum,257)==ESP_OK);
    memset(maximum,0,sizeof(maximum));assert(native_ie[2][0]==221 && native_length[2]==257);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK);
    before=native_calls;
    assert(esp32_mquickjs_wifi_radio_vendor_ie_set(WIFI_IF_STA,WIFI_VND_IE_TYPE_PROBE_REQ,0,true,element,5)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_set(WIFI_IF_STA,WIFI_VND_IE_TYPE_BEACON,0,true,element,6)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_set(WIFI_IF_STA,WIFI_VND_IE_TYPE_PROBE_REQ,2,true,element,6)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_set(WIFI_IF_STA,WIFI_VND_IE_TYPE_PROBE_REQ,0,false,element,6)==ESP_ERR_INVALID_ARG);
    assert(native_calls==before && !owners() && !locks && !critical);return 0;
}
