
#define CONFIG_IDF_TARGET "esp32c3"
static unsigned capability_calls;
static const char *esp_get_idf_version(void) {return "test-idf";}
static JSValue js_wifi_capabilities(JSContext *ctx,JSValue *self,int argc,JSValue *argv) {
    assert(!self&&!argc&&!argv);capability_calls++;return JS_NewString(ctx,"current-build");
}
