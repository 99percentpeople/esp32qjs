
typedef int esp_err_t;
#define ESP_OK 0
typedef struct { bool restart_required; } esp32_mquickjs_wifi_radio_status_t;
static bool driver_restart_required;
static int s_ap_detach_error;
static const char *s_ap_stage = "init";
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *s) {
    s->restart_required=driver_restart_required;return ESP_OK;
}
static JSValue esp32_mquickjs_throw_native_error(JSContext *ctx,const char *code,
    const char *operation,const char *message,JSValue details) {
    (void)ctx;(void)message;assert(!strcmp(code,"WIFI_AP_FAILED"));
    assert(!strcmp(operation,"wifi.startAP"));return details;
}
