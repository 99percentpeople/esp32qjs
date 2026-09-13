
static void *s_ap_netif;
static esp32_mquickjs_wifi_radio_lease_t s_ap_lease;
static esp32_mquickjs_wifi_radio_lifecycle_t s_ap_lifecycle,s_ap_coordinator;
static struct {uint32_t generation,identity;} s_ap_raw_owner;
static bool s_ap_cleanup_pending,helper_ready=true;
static const char *s_ap_stage;
static esp_err_t s_ap_detach_error,prepare_error,retire_error;
static unsigned prepares,retires,finishes;
static bool station_live;
static unsigned station_prepares,station_retires;
static esp_err_t station_prepare_error,station_retire_error;
static bool esp32_mquickjs_wifi_raw_tx_ap_ready(void) {return helper_ready;}
static esp_err_t esp32_mquickjs_wifi_retire_for_configuration(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    esp_err_t err=esp32_mquickjs_wifi_radio_check_stopped_lifecycle(t,true);if(err)return err;
    ++station_retires;if(station_retire_error)return station_retire_error;
    station_live=false;return ESP_OK;
}
static esp_err_t esp32_mquickjs_wifi_prepare_for_configuration(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    esp_err_t err=esp32_mquickjs_wifi_radio_check_stopped_lifecycle(t,false);if(err)return err;
    assert(!station_live);++station_prepares;station_live=true;return station_prepare_error;
}
static esp_err_t wifi_ap_prepare_netif(void) {
    assert(!locks && !critical && !s_ap_netif && s_radio.lifecycle.identity);++prepares;
    s_ap_netif=(void *)1;return prepare_error;
}
static esp_err_t wifi_ap_retire_netif(void) {
    assert(!locks && !critical && !s_radio.started && !s_radio.stop_required);++retires;
    if(retire_error)return retire_error;s_ap_netif=NULL;return ESP_OK;
}
/* Final shared lifecycle release is covered by its own production fixture. */
static esp_err_t esp32_mquickjs_wifi_radio_finish_lifecycle(esp32_mquickjs_wifi_radio_lifecycle_t *t,bool shutdown) {
    assert(!shutdown && !s_ap_netif && !station_live && !s_tx_rate_lease.identity);++finishes;
    int err=esp32_mquickjs_wifi_radio_check_stopped_lifecycle(t,true);if(err)return err;
    memset(&s_radio.lifecycle,0,sizeof(s_radio.lifecycle));memset(t,0,sizeof(*t));return ESP_OK;
}
