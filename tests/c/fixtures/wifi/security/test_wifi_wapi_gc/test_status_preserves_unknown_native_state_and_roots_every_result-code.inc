
static bool s_wifi_configuration_cleanup;
static struct{bool runtime_cleanup_pending;}s_wifi_state;
static esp32_mquickjs_wifi_wapi_status_t snapshot;
static void esp32_mquickjs_wifi_wapi_sdk_status(esp32_mquickjs_wifi_wapi_status_t*out){*out=snapshot;}
