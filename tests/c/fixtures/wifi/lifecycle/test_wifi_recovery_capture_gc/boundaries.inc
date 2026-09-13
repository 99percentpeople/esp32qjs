
typedef int esp_err_t,wifi_mode_t,wifi_config_t;
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 1
#define ESP_ERR_INVALID_STATE 2
typedef struct {unsigned generation,identity;} esp32_mquickjs_wifi_radio_lifecycle_t;
typedef struct {unsigned generation;bool restart_required;const char *fault_stage;int fault_error;} esp32_mquickjs_wifi_radio_status_t;
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *out) {
    *out=(esp32_mquickjs_wifi_radio_status_t){.generation=10};return ESP_OK;
}
static bool esp32_mquickjs_wifi_configuration_pending(void) {return true;}
static int esp32_mquickjs_wifi_ap_netif_cleanup_error(void) {return 0;}
static struct station_state {int sta_detach_error;} station;
static struct station_state *esp32_mquickjs_wifi_state(void) {return &station;}
static const char *esp_err_to_name(int error) {(void)error;return "injected-esp-error";}
static esp32_mquickjs_wifi_recovery_kind_t kind_case;
static bool timeout_case;
static const char *const methods[]={"wifi.action.recover","wifi.rawTx.recover","wifi.twt.recover","wifi.ftm.recover"};
static const char *const failures[]={"WIFI_ACTION_RECOVERY_FAILED","WIFI_RAW_TX_RECOVERY_FAILED","WIFI_TWT_RECOVERY_FAILED","WIFI_FTM_RECOVERY_FAILED"};
static const char *const timeouts[]={"WIFI_ACTION_RECOVERY_TIMEOUT","WIFI_RAW_TX_RECOVERY_TIMEOUT","WIFI_TWT_RECOVERY_TIMEOUT","WIFI_FTM_RECOVERY_TIMEOUT"};
static JSValue esp32_mquickjs_throw_native_error(JSContext *ctx,const char *code,const char *operation,const char *message,JSValue details) {
    assert(!strcmp(operation,methods[kind_case]));(void)message;
    assert(!strcmp(code,timeout_case?timeouts[kind_case]:failures[kind_case]));
    assert(JS_GetClassID(ctx,details)==JS_CLASS_OBJECT);return JS_ThrowTypeError(ctx,"%s",code);
}
