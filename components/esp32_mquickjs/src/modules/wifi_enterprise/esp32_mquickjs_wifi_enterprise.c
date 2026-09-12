#include "esp32_mquickjs_wifi_enterprise.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
#include "esp32_mquickjs_wifi_eap_config.h"
#include "esp32_mquickjs_wifi_eap_options.h"
#include "esp32_mquickjs_wifi_eap_radio.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp_heap_caps.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdatomic.h>

#define SET(o,k,v) do { if (!esp32_mquickjs_set_property_ref(ctx,o,k,v)) goto fail; } while (0)
struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_wifi_eap_config_token_t control;
    esp32_mquickjs_wifi_eap_profile_t *profile; /* Borrowed from exact config control. */
    esp32_mquickjs_wifi_eap_install_result_t result;
    uint64_t binding;
    uint32_t timeout_ms;
    esp32_mquickjs_wifi_eap_config_action_t action;
    atomic_bool done, cancelled;
    bool submitted, finalized;
    esp32_mquickjs_wifi_radio_lease_t owners[3];
};

static JSValue enterprise_revision(JSContext *ctx, uint64_t revision)
{
    char text[21]; snprintf(text, sizeof(text), "%" PRIu64, revision);
    return JS_NewString(ctx, text);
}

/* A busy config control may hold the Radio mutex while waiting for SDK. Do not
 * wait on it from status or a Future timeout: report unknown native fields. */
static JSValue enterprise_status(JSContext *ctx)
{
    esp32_mquickjs_wifi_eap_config_status_t config;
    esp32_mquickjs_wifi_eap_config_status(&config);
    esp32_mquickjs_wifi_eap_counts_t counts;
    esp32_mquickjs_wifi_eap_profile_counts(&counts);
    uint64_t binding = config.busy ? 0 : esp32_mquickjs_wifi_radio_eap_identity();
    esp32_mquickjs_wifi_eap_install_result_t native = {0};
    bool ready = false;
    if (binding) {
        ready = esp32_mquickjs_wifi_radio_eap_ready();
        (void)esp32_mquickjs_wifi_radio_eap_status(binding, &native);
    }
    JSGCRef ref;
    JSValue *value = JS_PushGCRef(ctx, &ref);
    *value = JS_NewObject(ctx);
    if (JS_IsException(*value)) goto fail;
    SET(value,"configured",JS_NewBool(config.configured));
    SET(value,"revision",enterprise_revision(ctx,config.revision));
    SET(value,"busy",JS_NewBool(config.busy));
    SET(value,"closing",JS_NewBool(config.closing));
    SET(value,"revisionExhausted",JS_NewBool(config.revision_exhausted));
    SET(value,"identityExhausted",JS_NewBool(config.identity_exhausted));
    SET(value,"binding",config.busy ? JS_NULL : JS_NewBool(binding != 0));
    SET(value,"enabled",config.busy || (binding && (!ready || !native.sdk.entered)) ? JS_NULL : JS_NewBool(binding && native.enabled));
    SET(value,"cleanupPending",config.busy ? JS_NULL : JS_NewBool(binding && !ready));
    SET(value,"profiles",JS_NewUint32(ctx,counts.profiles));
    SET(value,"reservedBytes",JS_NewUint32(ctx,counts.reserved_bytes));
    SET(value,"nativeObserved",JS_NewBool(native.sdk.entered));
    SET(value,"nativeResources",native.sdk.entered ? JS_NewUint32(ctx,native.sdk.resources) : JS_NULL);
    SET(value,"checkCertificateTime",native.sdk.entered && native.sdk.time_check_known
        ? JS_NewBool(!native.sdk.disable_time_check) : JS_NULL);
    SET(value,"error",binding && native.error ? JS_NewInt32(ctx,native.error) : JS_NULL);
    SET(value,"cleanupError",native.sdk.entered && native.sdk.cleanup_error ? JS_NewInt32(ctx,native.sdk.cleanup_error) : JS_NULL);
    SET(value,"controlError",native.sdk.entered && native.sdk.control_error ? JS_NewInt32(ctx,native.sdk.control_error) : JS_NULL);
    SET(value,"stage",binding && native.stage ? JS_NewString(ctx,native.stage) : JS_NULL);
    return JS_PopGCRef(ctx,&ref);
fail:
    JS_PopGCRef(ctx,&ref);return JS_EXCEPTION;
}

static const char *enterprise_action(esp32_mquickjs_wifi_eap_config_action_t action)
{
    return action == ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE ? "wifi.enterprise.enable" :
        action == ESP32_MQUICKJS_WIFI_EAP_CONFIG_DISABLE ? "wifi.enterprise.disable" : "wifi.enterprise.clear";
}

static JSValue enterprise_error(JSContext *ctx, const char *operation, const char *code,
    const char *stage, esp_err_t error, bool pending)
{
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx,&ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details)) goto fail;
    SET(details,"stage",JS_NewString(ctx,stage ? stage : "unknown"));
    SET(details,"espCode",JS_NewInt32(ctx,error));
    SET(details,"nativePending",JS_NewBool(pending));
    (void)esp32_mquickjs_throw_native_error(ctx,code,operation,
        "Enterprise operation failed; inspect status before retrying an in-flight SDK operation",*details);
fail:
    JS_PopGCRef(ctx,&ref);return JS_EXCEPTION;
}

JSValue js_wifi_enterprise_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;(void)argv;
    return argc ? JS_ThrowTypeError(ctx,"enterprise status expects no arguments") : enterprise_status(ctx);
}

JSValue js_wifi_enterprise_configure(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    if (argc != 1) return JS_ThrowTypeError(ctx,"enterprise configure expects one options object");
    esp32_mquickjs_wifi_eap_config_status_t config;
    esp32_mquickjs_wifi_eap_config_status(&config); /* Before any user getter. */
    if (config.busy || config.closing || config.revision_exhausted || esp32_mquickjs_wifi_radio_eap_identity())
        return enterprise_error(ctx,"wifi.enterprise.configure","WIFI_ENTERPRISE_FAILED","configuration-admission",ESP_ERR_INVALID_STATE,false);
    esp32_mquickjs_wifi_eap_profile_t *profile = NULL;
    if (!esp32_mquickjs_wifi_eap_capture(ctx,argv[0],&profile)) return JS_EXCEPTION;
    /* Getters may have scheduled another control. Do not wait on its SDK lock
     * in config_replace; reject the stale capture before querying Radio. */
    esp32_mquickjs_wifi_eap_config_status_t current;
    esp32_mquickjs_wifi_eap_config_status(&current);
    if (current.busy || current.closing || current.revision != config.revision) {
        esp32_mquickjs_wifi_eap_profile_release(profile);
        return enterprise_error(ctx,"wifi.enterprise.configure","WIFI_ENTERPRISE_FAILED","configuration-stale",ESP_ERR_INVALID_STATE,false);
    }
    /* The commit result is fully allocated before mutation. It describes the
     * accepted configuration revision; live driver status is a separate read. */
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx,&ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result,"configured",JS_TRUE);
    SET(result,"revision",enterprise_revision(ctx,config.revision + 1));
    esp_err_t error = esp32_mquickjs_wifi_eap_config_replace(config.revision,profile);
    esp32_mquickjs_wifi_eap_profile_release(profile);profile = NULL;
    if (error != ESP_OK) {
        (void)enterprise_error(ctx,"wifi.enterprise.configure","WIFI_ENTERPRISE_FAILED","configuration-commit",error,false);
        goto fail;
    }
    return JS_PopGCRef(ctx,&ref);
fail:
    esp32_mquickjs_wifi_eap_profile_release(profile);
    JS_PopGCRef(ctx,&ref);return JS_EXCEPTION;
}

JSValue js_wifi_enterprise_capabilities(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;(void)argv;
    if (argc) return JS_ThrowTypeError(ctx,"enterprise capabilities expects no arguments");
    JSGCRef ref,list_ref,item_ref;
    JSValue *value = JS_PushGCRef(ctx,&ref),*methods = JS_PushGCRef(ctx,&list_ref);
    JSValue *item = JS_PushGCRef(ctx,&item_ref);
    *value = JS_NewObject(ctx);*methods = JS_NewArray(ctx,0);
    if (JS_IsException(*value) || JS_IsException(*methods)) goto fail;
    static const char *const names[] = {"tls","ttls","peap","fast"};
    #if CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT
    const bool domain = true;
    unsigned count = 3;
#else
    const bool domain = false;
    unsigned count = 4;
#endif
    for (unsigned i=0;i<count;++i) {
        *item = JS_NewString(ctx,names[i]);
        if (JS_IsException(*item) || JS_IsException(JS_SetPropertyUint32(ctx,*methods,i,*item))) goto fail;
    }
    SET(value,"apiVersion",JS_NewString(ctx,"wifi-enterprise/1"));
    SET(value,"stability",JS_NewString(ctx,"candidate"));
    SET(value,"target",JS_NewString(ctx,CONFIG_IDF_TARGET));
    SET(value,"methods",*methods);
    SET(value,"domain",JS_NewBool(domain));
#if CONFIG_ESP_WIFI_SUITE_B_192
    SET(value,"suiteB192",JS_TRUE);
#else
    SET(value,"suiteB192",JS_FALSE);
#endif
#if CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT && CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    SET(value,"defaultCertificateBundle",JS_TRUE);
#else
    SET(value,"defaultCertificateBundle",JS_FALSE);
#endif
    SET(value,"maximumProfiles",JS_NewUint32(ctx,ESP32_MQUICKJS_WIFI_ENTERPRISE_MAX_PROFILES));
    SET(value,"maximumProfileBytes",JS_NewUint32(ctx,ESP32_MQUICKJS_WIFI_ENTERPRISE_MAX_PROFILE_BYTES));
    SET(value,"maximumRetainedBytes",JS_NewUint32(ctx,ESP32_MQUICKJS_WIFI_ENTERPRISE_MAX_RETAINED_BYTES));
    JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&list_ref);return JS_PopGCRef(ctx,&ref);
fail:
    JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&list_ref);JS_PopGCRef(ctx,&ref);return JS_EXCEPTION;
}

static void enterprise_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    if (atomic_load_explicit(&state->cancelled,memory_order_acquire)) {
        state->result.error = ESP_ERR_INVALID_STATE;state->result.stage = "cancelled-before-sdk";
    } else if (state->action == ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE) {
        if (state->binding) {
            state->result.error = esp32_mquickjs_wifi_radio_eap_status(state->binding,&state->result);
            if (!esp32_mquickjs_wifi_radio_eap_ready() || !state->result.enabled) state->result.error = ESP_ERR_INVALID_STATE;
        } else state->result.error = esp32_mquickjs_wifi_radio_eap_install(&state->owners[0],&state->owners[1],&state->owners[2],state->profile,&state->result);
    } else if (state->binding) state->result.error = esp32_mquickjs_wifi_radio_eap_clear(state->binding,&state->result);
    /* Background pool retains no runtime/Future pointer and emits no wake.
     * Last access to state: periodic poll may destroy it after this release. */
    atomic_store_explicit(&state->done,true,memory_order_release);
}

static bool enterprise_capture(JSContext *ctx, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output, esp32_mquickjs_wifi_eap_config_action_t action)
{
    *output = NULL;
    esp32_mquickjs_wifi_eap_config_status_t config;
    esp32_mquickjs_wifi_eap_config_status(&config);
    if (argc > 1) { JS_ThrowTypeError(ctx,"enterprise control expects optional timeout options");return false; }
    uint32_t timeout = 5000;
    if (argc && !JS_IsUndefined(argv[0].val)) {
        static const char *const keys[] = {"timeoutMs"};
        if (!esp32_mquickjs_validate_plain_options(ctx,argv[0].val,"enterprise control",keys,1)) return false;
        JSGCRef ref;JSValue *v=JS_PushGCRef(ctx,&ref);
        *v=JS_GetPropertyStr(ctx,argv[0].val,"timeoutMs");
        bool valid=!JS_IsException(*v) && (JS_IsUndefined(*v) || esp32_mquickjs_value_to_bounded_u32(ctx,*v,1,60000,&timeout));
        JS_PopGCRef(ctx,&ref);
        if (!valid) { if(!JS_HasException(ctx)) JS_ThrowTypeError(ctx,"invalid enterprise timeoutMs");return false; }
    }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!state) { JS_ThrowOutOfMemory(ctx);return false; }
    state->action=action;state->timeout_ms=timeout;
    atomic_init(&state->done,false);atomic_init(&state->cancelled,false);
    esp_err_t error=esp32_mquickjs_wifi_eap_config_begin(config.revision,action,&state->control,&state->profile);
    if (error != ESP_OK) {
        esp32_mquickjs_memory_payload_free(state);(void)enterprise_error(ctx,enterprise_action(action),"WIFI_ENTERPRISE_FAILED","control-admission",error,false);return false;
    }
    *output=state;return true;
}
#define CAPTURE(name,action) static bool name(JSContext *ctx,JSGCRef *self,int argc,JSGCRef *argv,esp32_mquickjs_future_driver_state_t **out) { (void)self;return enterprise_capture(ctx,argc,argv,out,action); }
CAPTURE(enterprise_enable_capture,ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE)
CAPTURE(enterprise_disable_capture,ESP32_MQUICKJS_WIFI_EAP_CONFIG_DISABLE)
CAPTURE(enterprise_clear_capture,ESP32_MQUICKJS_WIFI_EAP_CONFIG_CLEAR)
static bool enterprise_start(JSContext *ctx,esp32_mquickjs_runtime_t *runtime,esp32_mquickjs_future_token_t token,esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx;(void)runtime;(void)token;
    state->binding=esp32_mquickjs_wifi_radio_eap_identity();
    if (state->action == ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE && !state->binding) {
        state->result.error=esp32_mquickjs_wifi_eap_capture_owners(state->owners);
        if (state->result.error != ESP_OK) {
            state->result.stage="helper-admission";
            atomic_store_explicit(&state->done,true,memory_order_release);
        }
    }
    return true;
}
static esp32_mquickjs_future_poll_t enterprise_poll(esp32_mquickjs_future_driver_state_t *state)
{
    if (!state->submitted && !atomic_load_explicit(&state->done,memory_order_acquire)) {
        if (atomic_load_explicit(&state->cancelled,memory_order_acquire)) {
            state->result.error=ESP_ERR_INVALID_STATE;state->result.stage="cancelled";
            atomic_store_explicit(&state->done,true,memory_order_release);
        } else {
            bool ready=true;
            if (state->action != ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE && state->binding)
                state->result.error=esp32_mquickjs_wifi_eap_disconnect_ready(&ready);
            if (state->result.error != ESP_OK) {
                state->result.stage="disconnect";atomic_store_explicit(&state->done,true,memory_order_release);
            } else if (ready) {
                state->submitted=esp32_mquickjs_submit_background_worker(enterprise_worker,state);
                if (!state->submitted) { state->result.error=ESP_ERR_NO_MEM;state->result.stage="worker-queue";atomic_store_explicit(&state->done,true,memory_order_release); }
            }
        }
    }
    if (!atomic_load_explicit(&state->done,memory_order_acquire)) return ESP32_MQUICKJS_FUTURE_PENDING;
    if (!state->finalized) {
        bool discard=state->action==ESP32_MQUICKJS_WIFI_EAP_CONFIG_CLEAR && state->submitted && state->result.error==ESP_OK;
        esp_err_t error=esp32_mquickjs_wifi_eap_config_finish(&state->control,discard);
        if (state->result.error==ESP_OK) state->result.error=error;
        state->profile=NULL;state->finalized=true;
    }
    return ESP32_MQUICKJS_FUTURE_READY;
}
static JSValue enterprise_finish(JSContext *ctx,esp32_mquickjs_future_driver_state_t *state)
{
    return state->result.error != ESP_OK ? enterprise_error(ctx,enterprise_action(state->action),"WIFI_ENTERPRISE_FAILED",state->result.stage,state->result.error,false) : enterprise_status(ctx);
}
static esp32_mquickjs_cancel_result_t enterprise_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    atomic_store_explicit(&state->cancelled,true,memory_order_release);return ESP32_MQUICKJS_CANCEL_REQUESTED;
}
static void enterprise_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (!state) return;
    /* Core destroys queued captures without starting them, or only after poll
     * says a started worker no longer accesses this state. */
    if (state->control.identity) (void)esp32_mquickjs_wifi_eap_config_finish(&state->control,false);
    esp32_mquickjs_memory_payload_free(state);
}
static uint32_t enterprise_timeout(const esp32_mquickjs_future_driver_state_t *state) {return state->timeout_ms;}
static JSValue enterprise_timed_out(JSContext *ctx,esp32_mquickjs_future_driver_state_t *state,uint32_t ms)
{
    (void)ms;atomic_store_explicit(&state->cancelled,true,memory_order_release);
    return enterprise_error(ctx,enterprise_action(state->action),"WIFI_ENTERPRISE_TIMEOUT","deadline",ESP_ERR_TIMEOUT,
        state->submitted && !atomic_load_explicit(&state->done,memory_order_acquire));
}
#define DRIVER(capture_fn) {.memory_owner = "wireless.future", .capture =capture_fn,.start=enterprise_start,.poll=enterprise_poll,.finish=enterprise_finish,.cancel=enterprise_cancel,.destroy=enterprise_destroy,.timeout_ms=enterprise_timeout,.on_timeout=enterprise_timed_out}
static const esp32_mquickjs_future_driver_t s_enterprise_enable_driver=DRIVER(enterprise_enable_capture);
static const esp32_mquickjs_future_driver_t s_enterprise_disable_driver=DRIVER(enterprise_disable_capture);
static const esp32_mquickjs_future_driver_t s_enterprise_clear_driver=DRIVER(enterprise_clear_capture);

static JSValue enterprise_call(JSContext *ctx,int argc,JSValue *argv,const char *name)
{
    JSGCRef object_ref,method_ref;
    JSValue *object=JS_PushGCRef(ctx,&object_ref),*method=JS_PushGCRef(ctx,&method_ref);
    *object=JS_GetGlobalObject(ctx);
    *object=JS_GetPropertyStr(ctx,*object,"wifi");
    if (!JS_IsException(*object)) *object=JS_GetPropertyStr(ctx,*object,"enterprise");
    *method=JS_IsException(*object)?JS_EXCEPTION:JS_GetPropertyStr(ctx,*object,name);
    JSValue value=JS_IsException(*method)?JS_EXCEPTION:esp32_mquickjs_future_call_and_wait(ctx,esp32_mquickjs_get_active_runtime(),*method,*object,argc,argv);
    JS_PopGCRef(ctx,&method_ref);JS_PopGCRef(ctx,&object_ref);return value;
}
JSValue js_wifi_enterprise_enable(JSContext *ctx,JSValue *self,int argc,JSValue *argv) {(void)self;return enterprise_call(ctx,argc,argv,"enable");}
JSValue js_wifi_enterprise_disable(JSContext *ctx,JSValue *self,int argc,JSValue *argv) {(void)self;return enterprise_call(ctx,argc,argv,"disable");}
JSValue js_wifi_enterprise_clear(JSContext *ctx,JSValue *self,int argc,JSValue *argv) {(void)self;return enterprise_call(ctx,argc,argv,"clear");}

bool esp32_mquickjs_init_wifi_enterprise_runtime(JSContext *ctx,esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef object_ref,method_ref;
    JSValue *object=JS_PushGCRef(ctx,&object_ref),*method=JS_PushGCRef(ctx,&method_ref);
    bool ok=false;
    *object=JS_GetGlobalObject(ctx);*object=JS_GetPropertyStr(ctx,*object,"wifi");
    if (JS_IsException(*object)) goto done;
    *object=JS_GetPropertyStr(ctx,*object,"enterprise");if(JS_IsException(*object))goto done;
    const char *names[]={"enable","disable","clear"};
    const esp32_mquickjs_future_driver_t *drivers[]={&s_enterprise_enable_driver,&s_enterprise_disable_driver,&s_enterprise_clear_driver};
    for (unsigned i=0;i<3;++i) {
        *method=JS_GetPropertyStr(ctx,*object,names[i]);
        if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx,runtime,*method,drivers[i])) goto done;
    }
    ok=true;
done:
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx,"failed to register enterprise Future drivers");
    JS_PopGCRef(ctx,&method_ref);JS_PopGCRef(ctx,&object_ref);return ok;
}
#endif
