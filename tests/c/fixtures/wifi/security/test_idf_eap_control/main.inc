
static void clean(void) {
    assert(esp_wifi_sta_enterprise_disable()==ESP_OK);
    assert(!driver_enabled && !driver_callbacks && !methods && !gWpaSm.wpa_sm_eap_disable && !esp32qjs_eap_native_resources());
}
int main(void) {
    assert(!esp32qjs_eap_configuration_idle());
    on_wifi=true;assert(esp32qjs_eap_configuration_idle());
    gWpaSm.wpa_sm_eap_disable=esp_wifi_sta_enterprise_disable;
    assert(!esp32qjs_eap_configuration_idle());gWpaSm.wpa_sm_eap_disable=NULL;
    g_wpa_password=(void *)1;assert(!esp32qjs_eap_configuration_idle());g_wpa_password=NULL;on_wifi=false;
    /* No mutation/driver call when outer dispatch or API lock fails. */
    fail_dispatch=true;assert(esp_wifi_sta_enterprise_enable()==ESP_FAIL && !enables && !allocations);fail_dispatch=false;
    fail_alloc=true;assert(esp_wifi_sta_enterprise_enable()==ESP_ERR_NO_MEM && !enables && !live);
    clean();assert(live==1); /* Existing SDK API mutex remains allocated. */
    fail_lock=true;assert(esp_wifi_sta_enterprise_enable()==ESP_ERR_INVALID_STATE && !enables);fail_lock=false;
    /* Driver already changed when callback allocation fails. Cleanup still reachable. */
    fail_alloc=true;assert(esp_wifi_sta_enterprise_enable()==ESP_ERR_NO_MEM && driver_enabled && gWpaSm.wpa_sm_eap_disable);
    unsigned n=enables;assert(esp_wifi_sta_enterprise_enable()==ESP_ERR_INVALID_STATE && enables==n);clean();
    fail_methods=true;n=method_clears;assert(esp_wifi_sta_enterprise_enable()==ESP_FAIL && driver_enabled && !methods && method_clears==n+1);fail_methods=false;clean();
    assert(esp_wifi_sta_enterprise_enable()==ESP_OK && driver_enabled && driver_callbacks && methods);
    n=enables;assert(esp_wifi_sta_enterprise_enable()==ESP_OK && enables==n);
    /* Confirmed disable write precedes failed worker retirement; retry doesn't write again. */
    gEapSm=(void *)1;g_wpa_password=(void *)2;fail_retire=true;n=global_clears;
    assert(esp_wifi_sta_enterprise_disable()==77 && !driver_enabled && gEapSm && g_wpa_password && global_clears==n);
    unsigned before=disables;fail_retire=false;clean();assert(disables==before);
    /* Unregister failure keeps methods/globals; only that unfinished suffix repeats. */
    assert(esp_wifi_sta_enterprise_enable()==0);g_wpa_password=(void *)2;unregister_error=88;
    assert(esp_wifi_sta_enterprise_disable()==88 && driver_callbacks && g_wpa_password);
    before=disables;unregister_error=0;clean();assert(disables==before);
    /* Unknown transport state blocks enable until an explicit clear succeeds. */
    driver_error=91;assert(esp_wifi_sta_enterprise_enable()==91 && esp32qjs_eap_driver_state<0);
    driver_error=0;n=enables;assert(esp_wifi_sta_enterprise_enable()==ESP_ERR_INVALID_STATE && enables==n);clean();
    /* Native task uses direct dispatch; worker task must not wait on Wi-Fi. */
    on_wifi=true;before=dispatches;assert(esp_wifi_sta_enterprise_enable()==0 && dispatches==before);assert(esp_wifi_sta_enterprise_disable()==0 && dispatches==before);on_wifi=false;
    s_wpa2_task_hdl=&eap_task_marker;on_eap=true;assert(esp32qjs_eap_on_worker());before=dispatches;assert(esp_wifi_sta_enterprise_enable()==ESP_ERR_INVALID_STATE && esp_wifi_sta_enterprise_disable()==ESP_ERR_INVALID_STATE && dispatches==before);on_eap=false;assert(!esp32qjs_eap_on_worker());s_wpa2_task_hdl=NULL;
    /* deattach stops before unrelated SAE/WPS/WPA cleanup when EAP clear fails. */
    assert(esp_wifi_sta_enterprise_enable()==0);gWpaSm.wpa_sm_wps_disable=clear_wps;gEapSm=(void *)1;fail_retire=true;
    on_wifi=true;assert(!wpa_deattach() && !sae_frees && !wps_frees && !wpa_frees && !roam_frees);on_wifi=false;
    fail_retire=false;on_wifi=true;assert(wpa_deattach() && sae_frees==1 && wps_frees==1 && wpa_frees==1 && roam_frees==1);on_wifi=false;
    /* Unexpected callback register result has ambiguous adoption. One bounded
     * quarantine, no second allocation, no repeated unregister after success. */
    for(unsigned adopt=0;adopt<2;adopt++) {
        register_error=93;adopt_on_error=adopt;assert(esp_wifi_sta_enterprise_enable()==93);
        assert(esp32qjs_eap_callback_quarantine);n=allocations;
        assert(esp_wifi_sta_enterprise_enable()==ESP_ERR_INVALID_STATE && allocations==n);
        assert(esp_wifi_sta_enterprise_disable()==ESP_ERR_INVALID_STATE && !driver_callbacks);
        before=unregisters;assert(esp_wifi_sta_enterprise_disable()==ESP_ERR_INVALID_STATE && unregisters==before);
        assert(live==1+!adopt);
        /* Fixture-only process-reset cleanup: the injected driver knows actual
         * adoption. Production deliberately cannot free this ambiguous pointer. */
        if(!adopt)os_free(esp32qjs_eap_callback_quarantine);
        esp32qjs_eap_callback_quarantine=NULL;register_error=0;clean();
    }
    assert(esp32qjs_eap_native_control_error()==0 && live==1 && !api_locked);
    os_free(s_wpa2_api_lock);s_wpa2_api_lock=NULL;assert(!live);return 0;
}
