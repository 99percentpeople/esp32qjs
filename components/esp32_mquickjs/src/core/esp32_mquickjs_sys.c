#include "esp32_mquickjs_sys.h"
#include "esp32_mquickjs_core.h"

#include <limits.h>
#include <string.h>

#include "bootloader_random.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/private/ctr_drbg.h"
#include "mbedtls/platform_util.h"

#define ESP32_MQUICKJS_MAX_SCOPED_TIMEOUT_MS 60000

static mbedtls_ctr_drbg_context s_secure_random;
static bool s_secure_random_initialized;

static int esp32_secure_random_entropy(void *opaque,
                                       unsigned char *output,
                                       size_t output_length)
{
    (void)opaque;
    esp_fill_random(output, output_length);
    return 0;
}

bool esp32_mquickjs_init_secure_random(JSContext *ctx)
{
    static const unsigned char personalization[] = "esp32qjs.randomHex.v1";
    int result;

    if (s_secure_random_initialized) {
        return true;
    }

    mbedtls_ctr_drbg_init(&s_secure_random);
    /* Seed before any JS-visible RF or ADC module can start using the SAR ADC. */
    bootloader_random_enable();
    result = mbedtls_ctr_drbg_seed(&s_secure_random,
                                   esp32_secure_random_entropy,
                                   NULL,
                                   personalization,
                                   sizeof(personalization) - 1U);
    bootloader_random_disable();
    if (result != 0) {
        mbedtls_ctr_drbg_free(&s_secure_random);
        JS_ThrowInternalError(ctx, "failed to initialize secure random generator: %d", result);
        return false;
    }

    /* Avoid reseeding later while Wi-Fi or ADC may own the entropy hardware. */
    mbedtls_ctr_drbg_set_reseed_interval(&s_secure_random, INT_MAX);
    s_secure_random_initialized = true;
    return true;
}

static const char *esp32_chip_model_name(void)
{
    esp_chip_info_t chip_info;

    esp_chip_info(&chip_info);
    switch (chip_info.model) {
    case CHIP_ESP32:
        return "ESP32";
    case CHIP_ESP32S2:
        return "ESP32-S2";
    case CHIP_ESP32S3:
        return "ESP32-S3";
    case CHIP_ESP32C3:
        return "ESP32-C3";
    case CHIP_ESP32C2:
        return "ESP32-C2";
    case CHIP_ESP32C6:
        return "ESP32-C6";
    case CHIP_ESP32H2:
        return "ESP32-H2";
    case CHIP_ESP32P4:
        return "ESP32-P4";
    case CHIP_ESP32C5:
        return "ESP32-C5";
    case CHIP_ESP32C61:
        return "ESP32-C61";
    default:
        return "ESP32";
    }
}

static JSValue sys_make_features_object(JSContext *ctx)
{
    JSGCRef features_ref;
    JSValue *features;

    features = JS_PushGCRef(ctx, &features_ref);
    *features = JS_NewObject(ctx);
    if (JS_IsException(*features)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, features, "fs",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_FS)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "nvs",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_NVS)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "gpio",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_GPIO)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "ledc",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_LEDC)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "adc",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_ADC)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "dac",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_DAC)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "i2c",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_I2C)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "spi",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_SPI)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "uart",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_UART)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "usbSerial",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "socket",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "websocket",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "wifi",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_WIFI)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "httpServer",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "displayBuffer",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_DISPLAY_BUFFER)) ||
        !esp32_mquickjs_set_property_ref(ctx, features, "http",
                                     JS_NewBool(CONFIG_ESP32_MQUICKJS_FEATURE_HTTP))) {
        goto fail;
    }

    return JS_PopGCRef(ctx, &features_ref);

fail:
    JS_PopGCRef(ctx, &features_ref);
    return JS_EXCEPTION;
}

static JSValue sys_make_info_object(JSContext *ctx)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    JSGCRef info_ref;
    JSValue *info;
    JSGCRef features_ref;
    JSValue *features;
    uint32_t flash_size = 0;
    bool psram_enabled = false;
    size_t total_psram = 0;
    size_t free_psram = 0;
    size_t total_internal_heap = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t free_internal_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint32_t js_heap_size = runtime != NULL ? (uint32_t)runtime->js_heap_size : 0;
    const char *js_heap_region = (runtime != NULL && runtime->js_heap_in_psram) ? "psram" : "internal";
    bool littlefs_mounted = runtime != NULL && runtime->littlefs_mounted;
    bool auto_run_index_js = runtime != NULL && runtime->auto_run_startup_script;
    bool format_littlefs_on_mount_fail =
        runtime != NULL && runtime->format_littlefs_on_mount_fail;
    bool repl_enabled = runtime != NULL && runtime->repl_enabled;

#ifdef CONFIG_SPIRAM
    psram_enabled = esp_psram_is_initialized();
    total_psram = psram_enabled ? esp_psram_get_size() : 0;
    free_psram = psram_enabled ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM) : 0;
#endif

    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        flash_size = 0;
    }

    info = JS_PushGCRef(ctx, &info_ref);
    features = JS_PushGCRef(ctx, &features_ref);
    *info = JS_NewObject(ctx);
    if (JS_IsException(*info)) {
        goto fail;
    }
    *features = sys_make_features_object(ctx);
    if (JS_IsException(*features)) {
        goto fail;
    }
    if (!esp32_mquickjs_set_property_ref(ctx, info, "runtimeVersion",
                                         JS_NewString(ctx, ESP32QJS_VERSION)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "hostApiVersion",
                                         JS_NewUint32(ctx, ESP32QJS_HOST_API_VERSION)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "board",
                                         JS_NewString(ctx, ESP32_MQUICKJS_BOARD_NAME)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "chip",
                                         JS_NewString(ctx, esp32_chip_model_name())) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "features", *features) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "userLedPin",
                                         JS_NewInt32(ctx, ESP32_MQUICKJS_USER_LED_PIN)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "userLedActiveLow",
                                         JS_NewBool(ESP32_MQUICKJS_USER_LED_ACTIVE_LOW)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "scriptsDir",
                                         JS_NewString(ctx,
                                             runtime != NULL && runtime->fs_root[0] != '\0'
                                                 ? runtime->fs_root
                                                 : ESP32_MQUICKJS_LITTLEFS_BASE_PATH)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "flashSize",
                                         JS_NewUint32(ctx, flash_size)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "psramEnabled",
                                         JS_NewBool(psram_enabled)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "psramSize",
                                         JS_NewUint32(ctx, (uint32_t)total_psram)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "freePsram",
                                         JS_NewUint32(ctx, (uint32_t)free_psram)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "totalInternalHeap",
                                         JS_NewUint32(ctx, (uint32_t)total_internal_heap)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "freeInternalHeap",
                                         JS_NewUint32(ctx, (uint32_t)free_internal_heap)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "jsHeapSize",
                                         JS_NewUint32(ctx, js_heap_size)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "jsHeapRegion",
                                         JS_NewString(ctx, js_heap_region)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "littlefsMounted",
                                         JS_NewBool(littlefs_mounted)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "replEnabled",
                                         JS_NewBool(repl_enabled)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "autoRunIndexJs",
                                         JS_NewBool(auto_run_index_js)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "formatLittlefsOnMountFail",
                                         JS_NewBool(format_littlefs_on_mount_fail)) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "freeHeap",
                                         JS_NewUint32(ctx, esp_get_free_heap_size())) ||
        !esp32_mquickjs_set_property_ref(ctx, info, "jsTimeMs",
                                         JS_NewInt64(ctx, esp_timer_get_time() / 1000))) {
        goto fail;
    }

    JS_PopGCRef(ctx, &features_ref);
    return JS_PopGCRef(ctx, &info_ref);

fail:
    JS_PopGCRef(ctx, &features_ref);
    JS_PopGCRef(ctx, &info_ref);
    return JS_EXCEPTION;
}

JSValue js_sys_info(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return sys_make_info_object(ctx);
}

JSValue js_sys_millis(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt64(ctx, esp_timer_get_time() / 1000);
}

JSValue js_sys_micros(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt64(ctx, esp_timer_get_time());
}

JSValue js_sys_freeHeap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, esp_get_free_heap_size());
}

JSValue js_sys_randomHex(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv)
{
    static const char hex_digits[] = "0123456789abcdef";
    uint8_t bytes[64];
    char encoded[(sizeof(bytes) * 2U) + 1U];
    int byte_count;
    int i;
    int random_result;
    JSValue result;

    (void)this_val;
    if (argc != 1 || JS_ToInt32(ctx, &byte_count, argv[0]) != 0 ||
        byte_count < 1 || byte_count > (int)sizeof(bytes)) {
        return JS_ThrowRangeError(ctx,
                                  "sys.randomHex(byteLength) expects 1..%u bytes",
                                  (unsigned)sizeof(bytes));
    }

    if (!s_secure_random_initialized) {
        return JS_ThrowInternalError(ctx, "secure random generator is not initialized");
    }
    random_result = mbedtls_ctr_drbg_random(&s_secure_random,
                                            bytes,
                                            (size_t)byte_count);
    if (random_result != 0) {
        mbedtls_platform_zeroize(bytes, sizeof(bytes));
        return JS_ThrowInternalError(ctx,
                                     "secure random generation failed: %d",
                                     random_result);
    }
    for (i = 0; i < byte_count; ++i) {
        encoded[i * 2] = hex_digits[bytes[i] >> 4];
        encoded[(i * 2) + 1] = hex_digits[bytes[i] & 0x0f];
    }
    encoded[byte_count * 2] = '\0';
    result = JS_NewStringLen(ctx, encoded, (size_t)byte_count * 2U);
    mbedtls_platform_zeroize(bytes, sizeof(bytes));
    mbedtls_platform_zeroize(encoded, sizeof(encoded));
    return result;
}

JSValue js_sys_withTimeout(JSContext *ctx,
                             JSValue *this_val,
                             int argc,
                             JSValue *argv)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    uint64_t previous_deadline_us;
    uint64_t previous_scoped_deadline_us;
    uint64_t requested_deadline_us;
    uint64_t effective_deadline_us;
    int timeout_ms;
    JSValue result;

    (void)this_val;
    if (argc < 2 || JS_ToInt32(ctx, &timeout_ms, argv[0]) != 0 ||
        timeout_ms <= 0 || timeout_ms > ESP32_MQUICKJS_MAX_SCOPED_TIMEOUT_MS) {
        return JS_ThrowRangeError(ctx,
                                  "sys.withTimeout(timeoutMs, callback) expects timeoutMs between 1 and 60000");
    }
    if (!JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx,
                                 "sys.withTimeout(timeoutMs, callback) expects a callback function");
    }
    if (runtime == NULL) {
        return JS_ThrowInternalError(ctx, "ESP32 runtime is not active");
    }

    previous_deadline_us = runtime->deadline_us;
    previous_scoped_deadline_us = runtime->scoped_deadline_us;
    requested_deadline_us = (uint64_t)esp_timer_get_time() +
                            ((uint64_t)(uint32_t)timeout_ms * 1000ULL);
    if (previous_deadline_us == 0 || requested_deadline_us < previous_deadline_us) {
        runtime->deadline_us = requested_deadline_us;
    }
    if (previous_scoped_deadline_us == 0 ||
        requested_deadline_us < previous_scoped_deadline_us) {
        runtime->scoped_deadline_us = requested_deadline_us;
    }
    effective_deadline_us = runtime->scoped_deadline_us;

    result = esp32_mquickjs_call(ctx, runtime, argv[1], JS_NULL, 0, NULL);
    if (effective_deadline_us > 0 &&
        (uint64_t)esp_timer_get_time() >= effective_deadline_us) {
        if (JS_IsException(result)) {
            (void)JS_GetException(ctx);
        }
        runtime->deadline_us = previous_deadline_us;
        runtime->scoped_deadline_us = previous_scoped_deadline_us;
        return JS_ThrowInternalError(ctx, "sys.withTimeout() deadline exceeded");
    }
    runtime->deadline_us = previous_deadline_us;
    runtime->scoped_deadline_us = previous_scoped_deadline_us;
    return result;
}
