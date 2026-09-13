
#define ESP_ERR_INVALID_ARG -21
#define WIFI_MODE_APSTA 3
static bool s_wifi_ap_stop_cleanup,s_ap_partial_stop;
static int partial_fail,partial_admissions,partial_quiesce_calls,partial_mutations,adoptions;
static bool partial_submitted;
static int esp32_mquickjs_wifi_radio_begin_ap_stop(const esp32_mquickjs_wifi_radio_lease_t *app,
    const esp32_mquickjs_wifi_radio_lease_t *sta,const esp32_mquickjs_wifi_radio_lease_t *ap,
    esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    partial_admissions++;assert(app->identity==1 && sta->identity==2 && ap->identity==3);
    return esp32_mquickjs_wifi_radio_begin_lifecycle(app,sta,ap,token);
}
static int esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    assert(exact(t));partial_quiesce_calls++;
    if(!partial_submitted){partial_mutations++;partial_submitted=true;}
    if(partial_fail==1)return -11;
    partial_quiesced=true;fail_at=partial_fail==2?2:0;return 0;
}
static int esp32_mquickjs_wifi_radio_check_ap_stopped_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    return exact(t) && partial_quiesced?0:ESP_ERR_INVALID_STATE;
}
static int esp32_mquickjs_wifi_radio_finish_ap_stop(esp32_mquickjs_wifi_radio_lifecycle_t *t,
    esp32_mquickjs_wifi_radio_lease_t *ap) {
    assert(exact(t) && partial_quiesced && !s_ap_netif && ap==&s_ap_lease);
    if(partial_fail==3)return -13;
    esp32_mquickjs_wifi_radio_release(ap);
    memset(t,0,sizeof(*t));memset(&native_token,0,sizeof(native_token));return 0;
}
static int esp32_mquickjs_wifi_radio_adopt_ap_stop(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    assert(exact(t) && s_wifi_application.identity==1 && s_wifi_state.radio_lease.identity==2 && s_ap_lease.identity==3);
    adoptions++;return 0;
}
static int wifi_cleanup_failed_init(void) { assert(0);return -1; }
typedef int JSContext,JSValue;
#define JS_UNDEFINED -1
#define JS_EXCEPTION -99
static bool timeout_capture_error;
static uint32_t timeout_budget=1000;
static int wait_scope,wait_begins,wait_ends,wait_begin_error;
static bool esp32_mquickjs_wifi_capture_stop_ap_timeout(JSContext *ctx,JSValue value,uint32_t *out) {
    (void)ctx;assert(value==JS_UNDEFINED);if(timeout_capture_error)return false;*out=timeout_budget;return true;
}
static int esp32_mquickjs_wifi_wait_begin(uint32_t timeout) {
    assert(timeout==timeout_budget && !wait_scope);wait_begins++;if(wait_begin_error)return wait_begin_error;wait_scope=1;return 0;
}
static void esp32_mquickjs_wifi_wait_end(void) { assert(wait_scope);wait_scope=0;wait_ends++; }

static int JS_ThrowTypeError(JSContext *c,const char *m) { (void)c;(void)m;return -98; }
static int esp32_mquickjs_wifi_make_status_object(JSContext *c) { (void)c;return 123; }
static int wifi_ap_error(JSContext *c,const char *op,int err) { (void)c;assert(!strcmp(op,"wifi.stopAP") && err);return -99; }
