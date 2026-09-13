
#define ESP32_MQUICKJS_WIFI_ROC_MAX_HANDLES 8
static bool esp32_mquickjs_wifi_roc_current_status(esp32_mquickjs_wifi_roc_status_t *out) {(void)out;return false;}
static void esp32_mquickjs_wifi_roc_counts(uint32_t *handles,bool *active,bool *cleanup) {*handles=0;*active=false;*cleanup=false;}
#define ESP32_MQUICKJS_MEMORY_EXTERNAL 1
#undef esp32_mquickjs_memory_wireless_alloc
static void *esp32_mquickjs_memory_wireless_alloc(const char *tag,size_t length,int policy,
    esp32_mquickjs_memory_budget_role_t role) {
    assert(!strcmp(tag,"wifi.action") && role==ESP32_MQUICKJS_MEMORY_BUDGET_TX && length>sizeof(wifi_action_tx_req_t) &&
        length<=sizeof(wifi_action_tx_req_t)+1476 && policy==1);
    return heap_caps_malloc(length,1);
}
static int esp32_mquickjs_wifi_action_receive(uint8_t *a,uint8_t *b,size_t c,uint8_t d) {
    (void)a;(void)b;(void)c;(void)d;return 0;
}
static JSValue action_error(JSContext *ctx,const char *code,esp_err_t error,const char *stage) {
    (void)error;(void)stage;return JS_ThrowTypeError(ctx,"%s",code);
}
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
static const char s_action_retired_lock;
static struct {bool active;esp_err_t error;const char *stage;} s_action_retired={true,23,"terminal-fence"};
void esp32_mquickjs_wifi_radio_action_snapshot(esp32_mquickjs_wifi_action_lane_t *out) {
    *out=(esp32_mquickjs_wifi_action_lane_t){.identity=45,.generation=7,.operation_id=255,
        .submitted=true,.terminal=true,.cancel_written=true,.next_identity=0};
}
