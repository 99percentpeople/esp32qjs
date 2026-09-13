
static void assert_empty(void){assert(!gEapSm && !worker && !live && !(esp32qjs_eap_native_resources() & ~ESP32_MQUICKJS_WIFI_EAP_SDK_DRIVER_UNKNOWN) && !esp32qjs_eap_cleanup_error && !lock_held);}
static void init(void){allocations=fail_at=config_frees=0;assert(eap_peer_sm_init()==ESP_OK);assert(gEapSm && worker && esp32qjs_eap_task_started);}
int main(void) {
    unsigned count;
    init();count=allocations;
    assert(wpa2_post(SIG_WPA2_MAX,0)==ESP_ERR_INVALID_STATE);
    in_worker=true;assert(wpa2_task_delete(NULL)==ESP_ERR_INVALID_STATE);in_worker=false;
    eap_peer_sm_deinit();assert_empty();
    for(unsigned nth=1;nth<=count;nth++) {
        allocations=config_frees=0;fail_at=nth;
        assert(eap_peer_sm_init()!=ESP_OK);assert_empty();
    }
    /* Queue rejection keeps SM, crypto and global credentials for retry. */
    init();struct eap_sm *old=gEapSm;unsigned owners=live;queue_fail=true;
    unsigned old_resets=global_resets;
    assert(eap_client_disable_fn(NULL)!=ESP_OK);
    assert(gEapSm==old && live==owners && !config_frees && esp32qjs_eap_retiring && esp32qjs_eap_cleanup_error!=ESP_OK && global_resets==old_resets);
    assert(eap_peer_sm_init()==ESP_ERR_INVALID_STATE && gEapSm==old && live==owners);
    u8 packet[6]={0};assert(eap_sm_rx_eapol(packet,packet,6,packet)!=ESP_OK && live==owners);
    queue_fail=false;assert(eap_client_disable_fn(NULL)==ESP_OK);assert_empty();
    /* A stale shared RX ack is not a worker-exit ack. */
    init();*(unsigned *)s_wifi_wpa2_sync_sem=1;eap_peer_sm_deinit();assert_empty();
    /* If waiting fails, later task completion alone still requires consuming
     * the dedicated ack before deleting semaphores or SM storage. */
    init();old=gEapSm;wait_fail=true;eap_peer_sm_deinit();
    assert(gEapSm==old && esp32qjs_eap_task_started && !config_frees);
    run_worker();assert(!worker && !s_wpa2_task_hdl && esp32qjs_eap_task_started && *(unsigned *)esp32qjs_eap_exit_sem==1);
    eap_peer_sm_deinit();assert_empty();
    /* Unregister failure does not reset borrowed global credentials. */
    init();old_resets=global_resets;unregister_error=123;esp32qjs_eap_callbacks=true;
    assert(eap_client_disable_fn(NULL)==123 && !gEapSm && global_resets==old_resets);
    unregister_error=0;assert(eap_client_disable_fn(NULL)==0);assert_empty();
    esp32_mquickjs_wifi_eap_sdk_snapshot_t snapshot;
    dispatch_fail=true;assert(esp32_mquickjs_wifi_eap_sdk_snapshot(&snapshot)!=ESP_OK && !snapshot.entered && snapshot.resources==UINT32_MAX);
    dispatch_fail=false;assert(esp32_mquickjs_wifi_eap_sdk_snapshot(&snapshot)==ESP_OK && snapshot.entered && !snapshot.resources);
    init();assert(esp32_mquickjs_wifi_eap_sdk_snapshot(&snapshot)==ESP_OK && (snapshot.resources&ESP32_MQUICKJS_WIFI_EAP_SDK_EXIT_PENDING));
    eap_peer_sm_deinit();assert_empty();return 0;
}
