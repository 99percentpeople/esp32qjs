#include "utils/esp32_mquickjs_tls_error.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP || CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET || \
    CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET

#include "esp32_mquickjs_core.h"

#include <string.h>
#include <time.h>

#include "esp_tls_errors.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
#include "esp_http_client.h"
#endif

#define TLS_VALID_AFTER_UNIX 1577836800LL
#define TLS_VERIFY_CAPTURE_SLOTS 16U

typedef struct {
    TaskHandle_t task;
    uint32_t flags;
} tls_verify_capture_t;

static portMUX_TYPE s_tls_verify_lock = portMUX_INITIALIZER_UNLOCKED;
static tls_verify_capture_t s_tls_verify_captures[TLS_VERIFY_CAPTURE_SLOTS];
static size_t s_tls_verify_next_slot;

/* ESP-IDF keeps this symbol global but does not expose it in the public
 * header. The wrapper delegates every decision to the original bundle
 * verifier and only records the flags that remain afterwards. */
extern int esp_crt_verify_callback(void *buf, mbedtls_x509_crt *crt,
                                   int depth, uint32_t *flags);

static tls_verify_capture_t *tls_verify_capture_slot_locked(
    TaskHandle_t task, bool create)
{
    size_t i;

    for (i = 0; i < TLS_VERIFY_CAPTURE_SLOTS; ++i) {
        if (s_tls_verify_captures[i].task == task) {
            return &s_tls_verify_captures[i];
        }
    }
    if (!create) {
        return NULL;
    }
    i = s_tls_verify_next_slot++ % TLS_VERIFY_CAPTURE_SLOTS;
    s_tls_verify_captures[i].task = task;
    s_tls_verify_captures[i].flags = 0;
    return &s_tls_verify_captures[i];
}

static void tls_verify_capture_reset(void)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    tls_verify_capture_t *capture;

    taskENTER_CRITICAL(&s_tls_verify_lock);
    capture = tls_verify_capture_slot_locked(task, true);
    capture->flags = 0;
    taskEXIT_CRITICAL(&s_tls_verify_lock);
}

static uint32_t tls_verify_capture_take(void)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    tls_verify_capture_t *capture;
    uint32_t flags = 0;

    taskENTER_CRITICAL(&s_tls_verify_lock);
    capture = tls_verify_capture_slot_locked(task, false);
    if (capture != NULL) {
        flags = capture->flags;
        memset(capture, 0, sizeof(*capture));
    }
    taskEXIT_CRITICAL(&s_tls_verify_lock);
    return flags;
}

static bool tls_crt_is_synthetic_bundle_anchor(const mbedtls_x509_crt *crt)
{
#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY) && \
    CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY
    /* ESP-IDF reconstructs a trusted bundle key for cross-signed chains rather
     * than parsing a root certificate. That synthetic anchor intentionally has
     * no raw certificate or validity dates, so MBEDTLS_HAVE_TIME_DATE otherwise
     * treats its zero valid_to field as an expired certificate. Real peer and
     * intermediate certificates always retain raw DER and remain date-checked. */
    return crt != NULL && crt->version == 3 &&
           crt->raw.p == NULL && crt->raw.len == 0 &&
           crt->valid_from.year == 0 && crt->valid_to.year == 0;
#else
    (void)crt;
    return false;
#endif
}

static int tls_crt_verify_callback(void *buf, mbedtls_x509_crt *crt,
                                   int depth, uint32_t *flags)
{
    tls_verify_capture_t *capture;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    int result;

    if (flags != NULL &&
        tls_crt_is_synthetic_bundle_anchor(crt)) {
        *flags &= ~MBEDTLS_X509_BADCERT_EXPIRED;
    }
    result = esp_crt_verify_callback(buf, crt, depth, flags);

    /* Observe the final flags after ESP-IDF's bundle trust decision. */
    if (flags != NULL && *flags != 0) {
        taskENTER_CRITICAL(&s_tls_verify_lock);
        capture = tls_verify_capture_slot_locked(task, true);
        capture->flags |= *flags;
        taskEXIT_CRITICAL(&s_tls_verify_lock);
    }
    return result;
}

esp_err_t esp32_mquickjs_tls_crt_bundle_attach(void *conf)
{
    esp_err_t err = esp_crt_bundle_attach(conf);

    if (err == ESP_OK) {
        tls_verify_capture_reset();
        mbedtls_ssl_conf_verify((mbedtls_ssl_config *)conf,
                                tls_crt_verify_callback, NULL);
    }
    return err;
}

void esp32_mquickjs_tls_error_reset(esp32_mquickjs_tls_error_t *error)
{
    if (error != NULL) {
        memset(error, 0, sizeof(*error));
    }
}

void esp32_mquickjs_tls_error_set(esp32_mquickjs_tls_error_t *error,
                                  esp_err_t operation_error,
                                  esp_err_t esp_tls_error,
                                  int mbedtls_error,
                                  uint32_t verify_flags)
{
    if (error == NULL) {
        return;
    }
    error->operation_error = operation_error;
    error->esp_tls_error = esp_tls_error;
    error->mbedtls_error = mbedtls_error;
    error->verify_flags = verify_flags;
    error->present = operation_error != ESP_OK ||
                     esp_tls_error != ESP_OK ||
                     mbedtls_error != 0 || verify_flags != 0;
}

void esp32_mquickjs_tls_error_capture(esp32_mquickjs_tls_error_t *error,
                                      esp_tls_t *tls,
                                      esp_err_t operation_error)
{
    esp_tls_error_handle_t handle = NULL;
    esp_err_t esp_tls_error = ESP_OK;
    int mbedtls_error = 0;
    int verify_flags = 0;

    if (tls != NULL && esp_tls_get_error_handle(tls, &handle) == ESP_OK &&
        handle != NULL) {
        esp_tls_error = esp_tls_get_and_clear_last_error(
            handle, &mbedtls_error, &verify_flags);
    }
    esp32_mquickjs_tls_error_set(error, operation_error, esp_tls_error,
                                 mbedtls_error, (uint32_t)verify_flags);
    esp32_mquickjs_tls_error_merge_verify_flags(error);
}

void esp32_mquickjs_tls_error_merge_verify_flags(
    esp32_mquickjs_tls_error_t *error)
{
    uint32_t flags = tls_verify_capture_take();

    if (error != NULL && flags != 0) {
        error->verify_flags |= flags;
        error->present = true;
    }
}

static bool tls_error_is_timeout(const esp32_mquickjs_tls_error_t *error)
{
    return error->operation_error == ESP_ERR_TIMEOUT ||
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
           error->operation_error == ESP_ERR_HTTP_READ_TIMEOUT ||
#endif
           error->esp_tls_error == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT ||
           error->esp_tls_error == ESP_ERR_ESP_TLS_SERVER_HANDSHAKE_TIMEOUT ||
           error->mbedtls_error == MBEDTLS_ERR_SSL_TIMEOUT ||
           error->mbedtls_error == -MBEDTLS_ERR_SSL_TIMEOUT;
}

static bool tls_error_is_mbedtls(
    const esp32_mquickjs_tls_error_t *error, int expected)
{
    /* ESP-TLS records -ret, while some direct mbedTLS callers retain ret. */
    return error->mbedtls_error == expected ||
           error->mbedtls_error == -expected;
}

static bool tls_error_is_certificate_verify(
    const esp32_mquickjs_tls_error_t *error)
{
    return error->verify_flags != 0 ||
           tls_error_is_mbedtls(error,
                                MBEDTLS_ERR_X509_CERT_VERIFY_FAILED);
}

static bool tls_system_time_is_valid(void)
{
    return time(NULL) >= (time_t)TLS_VALID_AFTER_UNIX;
}

const char *esp32_mquickjs_tls_error_code(
    const esp32_mquickjs_tls_error_t *error)
{
    uint32_t time_flags = MBEDTLS_X509_BADCERT_EXPIRED |
                          MBEDTLS_X509_BADCERT_FUTURE |
                          MBEDTLS_X509_BADCRL_EXPIRED |
                          MBEDTLS_X509_BADCRL_FUTURE;

    if (error == NULL) {
        return "TLS_HANDSHAKE_FAILED";
    }
    if (error->operation_error == ESP_ERR_NO_MEM ||
        error->esp_tls_error == ESP_ERR_NO_MEM ||
        tls_error_is_mbedtls(error, MBEDTLS_ERR_SSL_ALLOC_FAILED)) {
        return "TLS_ALLOC_FAILED";
    }
    if ((error->verify_flags & time_flags) != 0 ||
        (!tls_system_time_is_valid() &&
         tls_error_is_certificate_verify(error))) {
        return "TLS_TIME_INVALID";
    }
    if (tls_error_is_certificate_verify(error)) {
        return "TLS_VERIFY_FAILED";
    }
    if (tls_error_is_timeout(error)) {
        return "TLS_TIMEOUT";
    }
    return "TLS_HANDSHAKE_FAILED";
}

JSValue esp32_mquickjs_throw_tls_error(
    JSContext *ctx, const char *operation,
    const esp32_mquickjs_tls_error_t *error)
{
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    JSValue result;
    const char *code = esp32_mquickjs_tls_error_code(error);
    esp_err_t esp_tls_error = error != NULL ? error->esp_tls_error : ESP_OK;
    int mbedtls_error = error != NULL ? error->mbedtls_error : 0;
    uint32_t verify_flags = error != NULL ? error->verify_flags : 0;

    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "operationError",
            JS_NewInt32(ctx, error != NULL ? error->operation_error
                                           : ESP_FAIL)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "espTlsError",
            JS_NewInt32(ctx, (int32_t)esp_tls_error)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "mbedtlsError",
            JS_NewInt32(ctx, (int32_t)mbedtls_error)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "verifyFlags", JS_NewUint32(ctx, verify_flags))) {
        JS_PopGCRef(ctx, &details_ref);
        return JS_EXCEPTION;
    }
    result = esp32_mquickjs_throw_native_error(
        ctx, code, operation, "TLS operation failed", *details);
    JS_PopGCRef(ctx, &details_ref);
    return result;
}

#endif
