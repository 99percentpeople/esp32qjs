#define ESP_OK 0
/* The Session lifecycle fixture supplies the real registry. This boundary only
 * supplies an immutable native snapshot to the production Future finish. */
typedef esp32_mquickjs_wifi_nan_message_status_t esp32_mquickjs_wifi_nan_message_t;
static void esp32_mquickjs_wifi_nan_message_status(esp32_mquickjs_wifi_nan_message_t *m,
    esp32_mquickjs_wifi_nan_message_status_t *s) { *s=*m; }
static JSValue nan_message_error(JSContext *ctx,esp32_mquickjs_wifi_nan_message_t *m,int e,bool timeout) {
    (void)m;(void)e;(void)timeout;return JS_ThrowInternalError(ctx,"native-operation-failed");
}
