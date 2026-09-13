
static struct {int sta_detach_error;} s_wifi_state;
static bool s_wifi_configuration_cleanup;
typedef struct {
    const char *fault_stage;bool restart_required;
    esp32_mquickjs_wifi_radio_config_result_t configuration,activation;
} esp32_mquickjs_wifi_radio_status_t;
static esp32_mquickjs_wifi_radio_status_t native_radio;
static int scenario,apply_calls,status_calls,diagnostic_calls,free_calls;
static bool driver_started;
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *out) {
    assert(!native_live);diagnostic_calls++;*out=native_radio;return ESP_OK;
}
static int esp32_mquickjs_wifi_ap_netif_cleanup_error(void) {return 0;}
static const char *esp_err_to_name(int error) {(void)error;return "injected-sdk-error";}
