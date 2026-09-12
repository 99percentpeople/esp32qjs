#include "esp32_mquickjs_wifi_diagnostics.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_sys.h"
#include "esp32_mquickjs_wifi.h"
#include "esp32_mquickjs_wifi_monitor.h"
#include "esp32_mquickjs_wifi_csi.h"
#include "esp32_mquickjs_wifi_ftm.h"
#include "esp32_mquickjs_wifi_twt.h"
#include "esp32_mquickjs_wifi_enterprise.h"
#include "esp32_mquickjs_wifi_smartconfig.h"
#include "esp32_mquickjs_wifi_wps.h"
#include "esp32_mquickjs_wifi_wps_ap.h"
#include "esp32_mquickjs_wifi_dpp.h"
#include "esp32_mquickjs_wifi_nan.h"
#include "esp32_mquickjs_wifi_vendor_ie.h"
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_memory.h"
#include "esp_idf_version.h"
#include "esp_timer.h"

/* Accessed only by the active JS runtime task; persists across runtime restart.
 * Reset serial is observation metadata, never an owner/operation identity. */
typedef struct {
    uint32_t count, unavailable_monitor_generations, unavailable_csi_generations;
    int64_t started_us, finished_us;
} wifi_diagnostics_reset_t;
static wifi_diagnostics_reset_t s_diagnostics_reset;

JSValue js_wifi_diagnostics_reset_counters(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.diagnostics.resetFrameworkCounters expects no arguments");
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    if (runtime == NULL) return JS_ThrowInternalError(ctx, "Wi-Fi diagnostics requires the active runtime");
    wifi_diagnostics_reset_t reset = {.started_us = esp_timer_get_time()};
    reset.count = s_diagnostics_reset.count == UINT32_MAX ? UINT32_MAX : s_diagnostics_reset.count + 1U;
    esp32_mquickjs_reset_event_queue_counters(runtime);
    esp32_mquickjs_wifi_watch_reset_counters();
    esp32_mquickjs_wifi_reset_connection_counters();
    reset.unavailable_monitor_generations = esp32_mquickjs_wifi_monitor_reset_counters();
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI
    reset.unavailable_csi_generations = esp32_mquickjs_wifi_csi_reset_counters();
#endif
    esp32_mquickjs_memory_reset_counters();
    reset.finished_us = esp_timer_get_time();
    s_diagnostics_reset = reset;
    /* No JS allocation after mutation: OOM cannot turn a completed reset into
     * a reported failure. Producers may increment again before we return. */
    return JS_UNDEFINED;
}

static JSValue wifi_diagnostics_reset_to_js(JSContext *ctx, const wifi_diagnostics_reset_t *reset)
{
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "count", JS_NewUint32(ctx, reset->count)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "startedUs", reset->count == 0 ? JS_NULL : JS_NewFloat64(ctx, (double)reset->started_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "finishedUs", reset->count == 0 ? JS_NULL : JS_NewFloat64(ctx, (double)reset->finished_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "unavailableMonitorGenerations", JS_NewUint32(ctx, reset->unavailable_monitor_generations)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "unavailableCsiGenerations", JS_NewUint32(ctx, reset->unavailable_csi_generations))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static bool wifi_diagnostics_capture_mask(JSContext *ctx, int argc, JSValue *argv, uint32_t *mask)
{
    double number = -1;
    if (argc > 1) goto invalid;
    if (argc == 1 && !JS_IsUndefined(argv[0])) {
        if (!JS_IsNumber(ctx, argv[0])) goto invalid;
        if (JS_ToNumber(ctx, &number, argv[0])) return false;
    }
    if (number == -1 || number == UINT32_MAX) {
        *mask = (uint32_t)WIFI_STATIS_ALL;
        return true;
    }
    const uint32_t bits = WIFI_STATIS_BUFFER | WIFI_STATIS_RXTX | WIFI_STATIS_HW |
        WIFI_STATIS_DIAG | WIFI_STATIS_PS;
    /* Comparison rejects NaN/infinity before the integer cast. */
    if (!(number >= 0 && number <= bits) || number != (uint32_t)number) goto invalid;
    *mask = (uint32_t)number;
    if ((*mask & ~bits) == 0U) return true;
invalid:
    JS_ThrowTypeError(ctx, "wifi.diagnostics.dumpDriverStats expects a statistics bit mask (0..31), -1 or 4294967295");
    return false;
}

JSValue js_wifi_diagnostics_dump_driver_stats(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    uint32_t mask;
    if (!wifi_diagnostics_capture_mask(ctx, argc, argv, &mask)) return JS_EXCEPTION;
    const char *stage;
    esp_err_t err = esp32_mquickjs_wifi_radio_dump_stats(mask, &stage);
    if (err == ESP_OK) return JS_TRUE;
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espCode", JS_NewInt32(ctx, err)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "stage", JS_NewString(ctx, stage)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "mask", JS_NewUint32(ctx, mask))) {
        JS_PopGCRef(ctx, &details_ref);
        return JS_EXCEPTION;
    }
    JSValue result = esp32_mquickjs_throw_native_error(ctx, "WIFI_DIAGNOSTICS_FAILED",
        "wifi.diagnostics.dumpDriverStats", "Wi-Fi driver statistics dump failed", *details);
    JS_PopGCRef(ctx, &details_ref);
    return result;
}

#include "esp32_mquickjs_wifi_coverage.inc"

static JSValue wifi_coverage_counts_to_js(JSContext *ctx, const uint32_t *counts)
{
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    for (unsigned i = 0; i < sizeof(s_wifi_coverage_fields) / sizeof(*s_wifi_coverage_fields); ++i) {
        if (!esp32_mquickjs_set_property_ref(ctx, result, s_wifi_coverage_fields[i],
            JS_NewUint32(ctx, counts[i]))) goto fail;
    }
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static JSValue wifi_coverage_rows_to_js(JSContext *ctx, const wifi_coverage_row_t *rows, size_t count)
{
    JSGCRef array_ref, item_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *array = JS_NewArray(ctx, (int)count);
    if (JS_IsException(*array)) goto fail;
    for (size_t i = 0; i < count; ++i) {
        *item = JS_NewObject(ctx);
        if (JS_IsException(*item) ||
            !esp32_mquickjs_set_property_ref(ctx, item, "name", JS_NewString(ctx, rows[i].name)) ||
            !esp32_mquickjs_set_property_ref(ctx, item, "counts", wifi_coverage_counts_to_js(ctx, rows[i].counts)) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *array, (uint32_t)i, *item))) goto fail;
    }
    JS_PopGCRef(ctx, &item_ref);
    return JS_PopGCRef(ctx, &array_ref);
fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_diagnostics_idf_api_coverage(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.diagnostics.idfApiCoverage expects no arguments");
    JSGCRef result_ref, array_ref, item_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
#define COVERAGE_SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)
    COVERAGE_SET(result, "apiVersion", JS_NewString(ctx, "wifi-idf-coverage/1"));
    COVERAGE_SET(result, "scope", JS_NewString(ctx, "reviewed-inventory"));
    COVERAGE_SET(result, "target", JS_NewString(ctx, CONFIG_IDF_TARGET));
    COVERAGE_SET(result, "idfVersion", JS_NewString(ctx, esp_get_idf_version()));
    COVERAGE_SET(result, "reviewedIdfRevision", JS_NewString(ctx, WIFI_COVERAGE_IDF_REVISION));
    COVERAGE_SET(result, "inventorySha256", JS_NewString(ctx, WIFI_COVERAGE_INVENTORY_SHA256));
    COVERAGE_SET(result, "mapSha256", JS_NewString(ctx, WIFI_COVERAGE_MAP_SHA256));
    COVERAGE_SET(result, "manifestSha256", JS_NewString(ctx, WIFI_COVERAGE_MANIFEST_SHA256));
    COVERAGE_SET(result, "total", wifi_coverage_counts_to_js(ctx, s_wifi_coverage_total));
#define COVERAGE_ROWS(name, rows) COVERAGE_SET(result, name, wifi_coverage_rows_to_js(ctx, rows, sizeof(rows) / sizeof(*rows)))
    COVERAGE_ROWS("headers", s_wifi_coverage_headers);
    COVERAGE_ROWS("tasks", s_wifi_coverage_tasks);
    COVERAGE_ROWS("referenceVariants", s_wifi_coverage_variants);
#undef COVERAGE_ROWS
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) goto fail;
    for (unsigned i = 0; s_wifi_coverage_gaps[i].header != NULL; ++i) {
        *item = JS_NewObject(ctx);
        if (JS_IsException(*item)) goto fail;
        COVERAGE_SET(item, "header", JS_NewString(ctx, s_wifi_coverage_gaps[i].header));
        COVERAGE_SET(item, "reason", JS_NewString(ctx, s_wifi_coverage_gaps[i].reason));
        if (JS_IsException(JS_SetPropertyUint32(ctx, *array, i, *item))) goto fail;
    }
    COVERAGE_SET(result, "unexpandedHeaders", *array);
    /* Reference-variant coverage is independent of the currently compiled
     * gates. Reuse the public capability provider, including its live country
     * availability semantics, rather than guessing a representative profile. */
    COVERAGE_SET(result, "capabilities", js_wifi_capabilities(ctx, NULL, 0, NULL));
#undef COVERAGE_SET
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

/* Each module copies its existing native ledger. No extra pool, history or
 * hidden Session owner is created. Independent observations deliberately do
 * not claim an atomic snapshot across driver, worker and runtime tasks. */
JSValue js_wifi_diagnostics_snapshot(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.diagnostics.snapshot expects no arguments");
    int64_t started_us = esp_timer_get_time();
    wifi_diagnostics_reset_t reset = s_diagnostics_reset;
    esp32_mquickjs_event_queue_status_t queues;
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    if (runtime == NULL || !esp32_mquickjs_get_event_queue_status(runtime, &queues))
        return JS_ThrowInternalError(ctx, "Wi-Fi diagnostics requires the active runtime");
    JSGCRef result_ref, queues_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *queue_result = JS_PushGCRef(ctx, &queues_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    *queue_result = JS_NewObject(ctx);
    if (JS_IsException(*queue_result)) goto fail;
#define DIAG_SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)
    DIAG_SET(queue_result, "open", JS_NewUint32(ctx, queues.open));
    DIAG_SET(queue_result, "dropped", JS_NewUint32(ctx, queues.dropped));
    DIAG_SET(queue_result, "queued", JS_NewUint32(ctx, queues.queued));
    DIAG_SET(queue_result, "capacity", JS_NewUint32(ctx, queues.capacity));
    DIAG_SET(queue_result, "highWater", JS_NewUint32(ctx, queues.high_water));
    DIAG_SET(result, "apiVersion", JS_NewString(ctx, "wifi-diagnostics/1"));
    DIAG_SET(result, "startedUs", JS_NewFloat64(ctx, (double)started_us));
    DIAG_SET(result, "runtimeQueues", *queue_result);
    DIAG_SET(result, "counterReset", wifi_diagnostics_reset_to_js(ctx, &reset));
    /* Sample the managed-memory ledger before allocating the larger module
     * results. It is global managed memory, not a Wi-Fi allocation subtotal. */
    DIAG_SET(result, "memory", js_sys_memory_manager(ctx, NULL, 0, NULL));
    DIAG_SET(result, "wifi", esp32_mquickjs_wifi_make_status_object(ctx));
    DIAG_SET(result, "monitor", esp32_mquickjs_wifi_monitor_diagnostics(ctx));
    DIAG_SET(result, "vendorIe", esp32_mquickjs_wifi_vendor_ie_status(ctx));
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI
    DIAG_SET(result, "csi", esp32_mquickjs_wifi_csi_diagnostics(ctx));
#else
    DIAG_SET(result, "csi", JS_NULL);
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    DIAG_SET(result, "ftmInitiator", js_wifi_ftm_global_status(ctx, NULL, 0, NULL));
#else
    DIAG_SET(result, "ftmInitiator", JS_NULL);
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    DIAG_SET(result, "ftmResponder", js_wifi_ftm_responder_offset_status(ctx, NULL, 0, NULL));
#else
    DIAG_SET(result, "ftmResponder", JS_NULL);
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    DIAG_SET(result, "twt", js_wifi_twt_status(ctx, NULL, 0, NULL));
#else
    DIAG_SET(result, "twt", JS_NULL);
#endif
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    DIAG_SET(result, "enterprise", js_wifi_enterprise_status(ctx, NULL, 0, NULL));
#else
    DIAG_SET(result, "enterprise", JS_NULL);
#endif
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    DIAG_SET(result, "smartConfig", js_wifi_smartconfig_global_status(ctx, NULL, 0, NULL));
    DIAG_SET(result, "wpsStation", js_wifi_wps_global_status(ctx, NULL, 0, NULL));
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
    DIAG_SET(result, "wpsAccessPoint", js_wifi_wps_ap_global_status(ctx, NULL, 0, NULL));
#else
    DIAG_SET(result, "wpsAccessPoint", JS_NULL);
#endif
#if CONFIG_ESP_WIFI_DPP_SUPPORT
    DIAG_SET(result, "dpp", js_wifi_dpp_global_status(ctx, NULL, 0, NULL));
#else
    DIAG_SET(result, "dpp", JS_NULL);
#endif
#else
    DIAG_SET(result, "smartConfig", JS_NULL);
    DIAG_SET(result, "wpsStation", JS_NULL);
    DIAG_SET(result, "wpsAccessPoint", JS_NULL);
    DIAG_SET(result, "dpp", JS_NULL);
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE
    DIAG_SET(result, "nan", js_wifi_nan_global_status(ctx, NULL, 0, NULL));
#else
    DIAG_SET(result, "nan", JS_NULL);
#endif
    DIAG_SET(result, "finishedUs", JS_NewFloat64(ctx, (double)esp_timer_get_time()));
#undef DIAG_SET
    JS_PopGCRef(ctx, &queues_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &queues_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}
#endif
