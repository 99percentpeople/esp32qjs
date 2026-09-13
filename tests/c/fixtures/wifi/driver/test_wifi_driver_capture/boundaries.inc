
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1
#define CONFIG_SOC_WIFI_SUPPORT_5G 1
#define ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU 100
#define ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU 60000
#define WIFI_IF_STA 0
#define WIFI_IF_AP 1
#define ESP_OK 0
#define ESP_ERR_NOT_SUPPORTED -2
typedef int esp_err_t,wifi_interface_t;
/* Storage layout is a parser boundary here; production SDK layout is checked
 * by the target build and the separate full Station/AP parser fixtures. */
typedef union {
    struct { uint8_t ssid[32],password[64];struct { bool required; } pmf_cfg; } sta;
    struct { uint8_t ssid[32],password[64],ssid_len,channel;uint16_t beacon_interval;
        struct { bool required; } pmf_cfg; } ap;
} wifi_config_t;
static int policy_calls,policy_failure,validator_error;
static bool force_required;
static bool native_requires_pmf(JSContext *ctx,JSValue options) {
    JSValue v=JS_GetPropertyStr(ctx,options,"pmf");
    if (force_required && !JS_IsUndefined(v)) {
        JSCStringBuf b;const char *s=JS_IsString(ctx,v) ? JS_ToCString(ctx,v,&b) : NULL;
        if(!s || strcmp(s,"required")){JS_ThrowTypeError(ctx,"injected policy conflict");return false;}
    }
    if(JS_IsUndefined(v))return force_required;
    JSCStringBuf b;const char *s=JS_IsString(ctx,v) ? JS_ToCString(ctx,v,&b) : NULL;
    if(!s || (strcmp(s,"optional") && strcmp(s,"required") && strcmp(s,"disabled"))){JS_ThrowTypeError(ctx,"injected policy invalid");return false;}
    return !strcmp(s,"required");
}
static bool esp32_mquickjs_wifi_parse_station_config_for_operation(JSContext *ctx,int argc,JSGCRef *args,
    wifi_config_t *out,uint32_t *timeout,const char *operation) {
    assert(!strcmp(operation,"wifi.configure")||!strcmp(operation,"wifi.driver.setInterfaceConfig"));
    assert(argc==2);policy_calls++;*timeout=1000;
    if(policy_failure){JS_ThrowTypeError(ctx,"injected policy rejection");return false;}
    out->sta.pmf_cfg.required=native_requires_pmf(ctx,args[1].val);
    if(JS_HasException(ctx))return false;
    memcpy(out->sta.ssid,"config",6);memcpy(out->sta.password,"native-secret",13);return true;
}
static bool esp32_mquickjs_wifi_parse_ap_config_for_operation(JSContext *ctx,JSValue options,wifi_config_t *out,const char *operation) {
    assert(!strcmp(operation,"wifi.configure")||!strcmp(operation,"wifi.driver.setInterfaceConfig"));
    policy_calls++;
    if(policy_failure){JS_ThrowTypeError(ctx,"injected policy rejection");return false;}
    out->ap.pmf_cfg.required=native_requires_pmf(ctx,options);
    if(JS_HasException(ctx))return false;
    memcpy(out->ap.ssid,"config",6);out->ap.ssid_len=6;
    memcpy(out->ap.password,"native-secret",13);out->ap.channel=1;out->ap.beacon_interval=100;return true;
}
static int esp32_mquickjs_wifi_radio_validate_ap_config(const wifi_config_t *config) {
    assert(config->ap.ssid_len>0 && config->ap.ssid_len<=32);return validator_error;
}
static JSValue esp32_mquickjs_wifi_throw_configuration_error(JSContext *ctx,int error,
    const char *option,const void *execution) {
    (void)error;(void)option;assert(!execution);
    return JS_ThrowTypeError(ctx,"injected native validation error");
}
static JSValue esp32_mquickjs_wifi_throw_operation_error(JSContext *ctx,const char *code,
    const char *operation,int error,int reason,uint32_t timeout) {
    (void)code;(void)operation;(void)error;(void)reason;(void)timeout;
    return JS_ThrowTypeError(ctx,"injected unsupported config");
}
static JSValue get_property_fault(JSContext *ctx,JSValue object,const char *key) {
    JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);*root=object;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,key);
    JS_PopGCRef(ctx,&r);return result;
}
static JSValue get_index_fault(JSContext *ctx,JSValue object,uint32_t index) {
    JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);*root=object;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyUint32(ctx,*root,index);
    JS_PopGCRef(ctx,&r);return result;
}
#define JS_GetPropertyStr get_property_fault
#define JS_GetPropertyUint32 get_index_fault
