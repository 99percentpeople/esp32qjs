
static bool ioctl_task,early_event,reenter,duplicate_enter;
int pm_get_sleep_type(void) {assert(ioctl_task && !locked);return 1;}
static int preflight_error,native_error,timer_error,late_timer_error,post_error,early_return;
static unsigned sdk_calls,native_calls,snapshots,posts;
static esp32_mquickjs_wifi_twt_tx_fault_t tx_fault;
static const wifi_itwt_setup_config_t *caller_input;
esp_err_t esp_wifi_sta_itwt_setup(wifi_itwt_setup_config_t *config);
esp_err_t __real_wifi_sta_itwt_setup_process(void *message);
esp_err_t esp32_mquickjs_wifi_twt_setup_timer_error(void) {return timer_error;}
esp_err_t esp32_mquickjs_wifi_twt_sdk_snapshot_native(esp32_mquickjs_wifi_twt_sdk_snapshot_t *out) {
    assert(ioctl_task && !locked);++snapshots;memset(out,0,sizeof(*out));
    for(unsigned i=0;i<8;++i)out->individual_ids[i]=out->individual_temporary_ids[i]=out->individual_pending_ids[i]=-1;
    out->tx.fault=tx_fault;out->individual_flow_bitmap=1;return ESP_OK; /* requested flow 0 gets SDK writeback 1 */
}
