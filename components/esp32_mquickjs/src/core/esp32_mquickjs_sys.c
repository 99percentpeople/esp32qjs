#include "esp32_mquickjs_sys.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_version.h"
#include "mquickjs_priv.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bootloader_random.h"
#include "esp_chip_info.h"
#include "esp_clk_tree.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/private/ctr_drbg.h"
#include "mbedtls/platform_util.h"
#include "soc/clk_tree_defs.h"

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

static bool esp32_hardware_id(char output[16])
{
    static const char hex_digits[] = "0123456789abcdef";
    uint8_t mac[6];
    size_t i;

    if (esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY) != ESP_OK) {
        return false;
    }
    output[0] = 'h';
    output[1] = 'w';
    output[2] = '-';
    for (i = 0; i < sizeof(mac); ++i) {
        output[3 + (i * 2)] = hex_digits[mac[i] >> 4];
        output[4 + (i * 2)] = hex_digits[mac[i] & 0x0f];
    }
    output[15] = '\0';
    return true;
}

static JSValue sys_profile_js_value(JSContext *ctx,
                                    const esp32_mquickjs_profile_value_t *value)
{
    switch (value->type) {
    case ESP32_MQUICKJS_PROFILE_VALUE_INTEGER:
        return JS_NewInt32(ctx, value->value.integer);
    case ESP32_MQUICKJS_PROFILE_VALUE_BOOLEAN:
        return JS_NewBool(value->value.boolean);
    case ESP32_MQUICKJS_PROFILE_VALUE_STRING:
        return JS_NewString(ctx,
                            value->value.string != NULL ? value->value.string : "");
    default:
        return JS_ThrowInternalError(ctx, "hardware profile contains an unsupported value type");
    }
}

static JSValue sys_profile_snapshot(JSContext *ctx)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    size_t count = esp32_mquickjs_profile_count();
    size_t index;

    *object = JS_NewObject(ctx);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    for (index = 0; index < count; ++index) {
        esp32_mquickjs_profile_value_t value;
        const char *key;
        JSValue property;

        if (!esp32_mquickjs_profile_get_at(index, &key, &value)) {
            JS_ThrowInternalError(ctx, "hardware profile enumeration failed");
            JS_PopGCRef(ctx, &object_ref);
            return JS_EXCEPTION;
        }
        property = sys_profile_js_value(ctx, &value);
        if (JS_IsException(property) ||
            !esp32_mquickjs_set_property_ref(ctx, object, key, property)) {
            JS_PopGCRef(ctx, &object_ref);
            return JS_EXCEPTION;
        }
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_sys_config(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_profile_value_t value;
    JSCStringBuf key_buf;
    const char *key;

    (void)this_val;
    if (argc == 0) {
        return sys_profile_snapshot(ctx);
    }
    if (!JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "sys.config(key) expects a string key");
    }
    key = JS_ToCString(ctx, argv[0], &key_buf);
    if (key == NULL) {
        return JS_EXCEPTION;
    }
    if (!esp32_mquickjs_profile_get(key, &value)) {
        return JS_UNDEFINED;
    }
    return sys_profile_js_value(ctx, &value);
}

static const char *sys_runtime_state_name(esp32_mquickjs_runtime_state_t state)
{
    switch (state) {
    case ESP32_MQUICKJS_RUNTIME_CREATED:
        return "created";
    case ESP32_MQUICKJS_RUNTIME_STARTING:
        return "starting";
    case ESP32_MQUICKJS_RUNTIME_RUNNING:
        return "running";
    case ESP32_MQUICKJS_RUNTIME_QUIESCING:
        return "quiescing";
    case ESP32_MQUICKJS_RUNTIME_RESTARTING:
        return "restarting";
    case ESP32_MQUICKJS_RUNTIME_STOPPING:
        return "stopping";
    case ESP32_MQUICKJS_RUNTIME_STOPPED:
        return "stopped";
    case ESP32_MQUICKJS_RUNTIME_FAILED:
        return "failed";
    default:
        return "failed";
    }
}

static const char *sys_control_action_name(esp32_mquickjs_control_action_t action)
{
    return action == ESP32_MQUICKJS_CONTROL_REBOOT ? "reboot" : "restart-runtime";
}

static const char *sys_restart_failure_action_name(
    esp32_mquickjs_restart_failure_action_t action)
{
    return action == ESP32_MQUICKJS_RESTART_FAILURE_STOP ? "stop" : "reboot";
}

static bool sys_get_host_status(esp32_mquickjs_host_status_t *status)
{
    return esp32_mquickjs_get_host_status(esp32_mquickjs_get_active_runtime(), status);
}

JSValue js_sys_version_get(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv,
                           int magic)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    switch (magic) {
    case 0:
        return JS_NewString(ctx, ESP32QJS_VERSION);
    case 1:
        return JS_NewUint32(ctx, ESP32QJS_HOST_API_VERSION);
    case 2:
        return JS_NewString(ctx, ESP32_MQUICKJS_ENGINE_VERSION);
    case 3:
        return JS_NewString(ctx, esp_get_idf_version());
    default:
        return JS_ThrowInternalError(ctx, "invalid sys.info.version getter");
    }
}

JSValue js_sys_feature_get(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv,
                           int magic)
{
    static const bool features[] = {
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_FS) && CONFIG_ESP32_MQUICKJS_FEATURE_FS
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_NVS) && CONFIG_ESP32_MQUICKJS_FEATURE_NVS
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_GPIO) && CONFIG_ESP32_MQUICKJS_FEATURE_GPIO
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_LEDC) && CONFIG_ESP32_MQUICKJS_FEATURE_LEDC
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_ADC) && CONFIG_ESP32_MQUICKJS_FEATURE_ADC
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_DAC) && CONFIG_ESP32_MQUICKJS_FEATURE_DAC
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_I2C) && CONFIG_ESP32_MQUICKJS_FEATURE_I2C
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_SPI) && CONFIG_ESP32_MQUICKJS_FEATURE_SPI
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_UART) && CONFIG_ESP32_MQUICKJS_FEATURE_UART
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL) && CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET) && CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET) && CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP) && CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_WIFI) && CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_HTTP) && CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER) && CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS) && CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_I2S) && CONFIG_ESP32_MQUICKJS_FEATURE_I2S
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA) && CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_RPC) && CONFIG_ESP32_MQUICKJS_FEATURE_RPC
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_RMT) && CONFIG_ESP32_MQUICKJS_FEATURE_RMT
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_TLS) && CONFIG_ESP32_MQUICKJS_FEATURE_TLS
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_NET) && CONFIG_ESP32_MQUICKJS_FEATURE_NET
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW) && CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_BLE) && CONFIG_ESP32_MQUICKJS_FEATURE_BLE
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI) && CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI
        true,
#else
        false,
#endif
#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP_JPEG) && CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP_JPEG
        true,
#else
        false,
#endif
    };

    (void)this_val;
    (void)argc;
    (void)argv;
    if (magic < 0 || magic >= (int)(sizeof(features) / sizeof(features[0]))) {
        return JS_ThrowInternalError(ctx, "invalid sys.info.features getter");
    }
    return JS_NewBool(features[magic]);
}

JSValue js_sys_hardware_get(JSContext *ctx,
                            JSValue *this_val,
                            int argc,
                            JSValue *argv,
                            int magic)
{
    char hardware_id[16];

    (void)this_val;
    (void)argc;
    (void)argv;
    if (magic == 0) {
        return esp32_hardware_id(hardware_id) ? JS_NewString(ctx, hardware_id) : JS_NULL;
    }
    if (magic == 1) {
        return JS_NewString(ctx, CONFIG_IDF_TARGET);
    }
    return JS_ThrowInternalError(ctx, "invalid sys.info.hardware getter");
}

JSValue js_sys_hardware_chip(JSContext *ctx,
                             JSValue *this_val,
                             int argc,
                             JSValue *argv)
{
    esp_chip_info_t info;
    JSGCRef chip_ref;
    JSGCRef revision_ref;
    JSGCRef capabilities_ref;
    JSValue *chip = JS_PushGCRef(ctx, &chip_ref);
    JSValue *revision = JS_PushGCRef(ctx, &revision_ref);
    JSValue *capabilities = JS_PushGCRef(ctx, &capabilities_ref);

    (void)this_val;
    (void)argc;
    (void)argv;
    esp_chip_info(&info);
    *chip = JS_NewObject(ctx);
    *revision = JS_NewObject(ctx);
    *capabilities = JS_NewObject(ctx);
    if (JS_IsException(*chip) || JS_IsException(*revision) ||
        JS_IsException(*capabilities) ||
        !esp32_mquickjs_set_property_ref(ctx, revision, "raw",
                                         JS_NewUint32(ctx, info.revision)) ||
        !esp32_mquickjs_set_property_ref(ctx, revision, "major",
                                         JS_NewUint32(ctx, info.revision / 100U)) ||
        !esp32_mquickjs_set_property_ref(ctx, revision, "minor",
                                         JS_NewUint32(ctx, info.revision % 100U)) ||
        !esp32_mquickjs_set_property_ref(ctx, capabilities, "embeddedFlash",
                                         JS_NewBool((info.features & CHIP_FEATURE_EMB_FLASH) != 0U)) ||
        !esp32_mquickjs_set_property_ref(ctx, capabilities, "wifi",
                                         JS_NewBool((info.features & CHIP_FEATURE_WIFI_BGN) != 0U)) ||
        !esp32_mquickjs_set_property_ref(ctx, capabilities, "ble",
                                         JS_NewBool((info.features & CHIP_FEATURE_BLE) != 0U)) ||
        !esp32_mquickjs_set_property_ref(ctx, capabilities, "bluetoothClassic",
                                         JS_NewBool((info.features & CHIP_FEATURE_BT) != 0U)) ||
        !esp32_mquickjs_set_property_ref(ctx, capabilities, "ieee802154",
                                         JS_NewBool((info.features & CHIP_FEATURE_IEEE802154) != 0U)) ||
        !esp32_mquickjs_set_property_ref(ctx, capabilities, "embeddedPsram",
                                         JS_NewBool((info.features & CHIP_FEATURE_EMB_PSRAM) != 0U)) ||
        !esp32_mquickjs_set_property_ref(ctx, chip, "model",
                                         JS_NewString(ctx, esp32_chip_model_name())) ||
        !esp32_mquickjs_set_property_ref(ctx, chip, "revision", *revision) ||
        !esp32_mquickjs_set_property_ref(ctx, chip, "cores",
                                         JS_NewUint32(ctx, info.cores)) ||
        !esp32_mquickjs_set_property_ref(ctx, chip, "capabilities", *capabilities)) {
        JS_PopGCRef(ctx, &capabilities_ref);
        JS_PopGCRef(ctx, &revision_ref);
        JS_PopGCRef(ctx, &chip_ref);
        return JS_EXCEPTION;
    }
    JS_PopGCRef(ctx, &capabilities_ref);
    JS_PopGCRef(ctx, &revision_ref);
    return JS_PopGCRef(ctx, &chip_ref);
}

JSValue js_sys_hardware_cpu(JSContext *ctx,
                            JSValue *this_val,
                            int argc,
                            JSValue *argv)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);

    (void)this_val;
    (void)argc;
    (void)argv;
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "configuredFrequencyHz",
            JS_NewUint32(ctx, (uint32_t)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_sys_hardware_flash(JSContext *ctx,
                              JSValue *this_val,
                              int argc,
                              JSValue *argv)
{
    uint32_t size_bytes = 0;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);

    (void)this_val;
    (void)argc;
    (void)argv;
    if (esp_flash_get_size(NULL, &size_bytes) != ESP_OK) {
        size_bytes = 0;
    }
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "sizeBytes",
                                         JS_NewUint32(ctx, size_bytes))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_sys_hardware_psram(JSContext *ctx,
                              JSValue *this_val,
                              int argc,
                              JSValue *argv)
{
    bool enabled = false;
    size_t size_bytes = 0;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);

    (void)this_val;
    (void)argc;
    (void)argv;
#ifdef CONFIG_SPIRAM
    enabled = esp_psram_is_initialized();
    if (enabled) {
        size_bytes = esp_psram_get_size();
    }
#endif
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "enabled",
                                         JS_NewBool(enabled)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "sizeBytes",
                                         JS_NewUint32(ctx, (uint32_t)size_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "mode",
            JS_NewString(ctx, enabled ? ESP32_MQUICKJS_PSRAM_MODE : "none"))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_sys_runtime_info_get(JSContext *ctx,
                                JSValue *this_val,
                                int argc,
                                JSValue *argv,
                                int magic)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();

    (void)this_val;
    (void)argc;
    (void)argv;
    if (magic != 0) {
        return JS_ThrowInternalError(ctx, "invalid sys.info.runtime getter");
    }
    return JS_NewUint32(ctx,
                        runtime != NULL ? runtime->eval_timeout_ms
                                        : ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS);
}

JSValue js_sys_runtime_info_heap(JSContext *ctx,
                                 JSValue *this_val,
                                 int argc,
                                 JSValue *argv)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);

    (void)this_val;
    (void)argc;
    (void)argv;
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "sizeBytes",
            JS_NewUint32(ctx, runtime != NULL ? (uint32_t)runtime->js_heap_size : 0U)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "region",
            JS_NewString(ctx,
                         runtime != NULL && runtime->js_heap_in_psram
                             ? "psram"
                             : "internal"))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_sys_runtime_info_task(JSContext *ctx,
                                 JSValue *this_val,
                                 int argc,
                                 JSValue *argv)
{
    esp32_mquickjs_host_status_t status;
    JSGCRef object_ref;
    JSValue *object;

    (void)this_val;
    (void)argc;
    (void)argv;
    if (!sys_get_host_status(&status)) {
        return JS_ThrowInternalError(ctx, "runtime host status is unavailable");
    }
    if (!status.managed) {
        return JS_NULL;
    }
    object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "name",
                                         JS_NewString(ctx, status.task_name)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "stackSizeBytes",
                                         JS_NewUint32(ctx, status.task_stack_size)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "priority",
                                         JS_NewUint32(ctx, status.task_priority)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "watchdogEnabled",
                                         JS_NewBool(status.task_watchdog_enabled))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_sys_runtime_info_startup(JSContext *ctx,
                                    JSValue *this_val,
                                    int argc,
                                    JSValue *argv)
{
    esp32_mquickjs_host_status_t status;
    JSGCRef object_ref;
    JSValue *object;

    (void)this_val;
    (void)argc;
    (void)argv;
    if (!sys_get_host_status(&status)) {
        return JS_ThrowInternalError(ctx, "runtime host status is unavailable");
    }
    if (!status.managed) {
        return JS_NULL;
    }
    object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "script",
                                         JS_NewString(ctx, status.startup_script)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "autorun",
                                         JS_NewBool(status.autorun_startup_script)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "repl",
                                         JS_NewBool(status.repl_enabled))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_sys_runtime_info_filesystem(JSContext *ctx,
                                       JSValue *this_val,
                                       int argc,
                                       JSValue *argv)
{
    esp32_mquickjs_host_status_t status;
    JSGCRef object_ref;
    JSGCRef secondary_ref;
    JSValue *object;
    JSValue *secondary;

    (void)this_val;
    (void)argc;
    (void)argv;
    if (!sys_get_host_status(&status)) {
        return JS_ThrowInternalError(ctx, "runtime host status is unavailable");
    }
    if (!status.managed) {
        return JS_NULL;
    }
    object = JS_PushGCRef(ctx, &object_ref);
    secondary = JS_PushGCRef(ctx, &secondary_ref);
    *object = JS_NewObject(ctx);
    *secondary = JS_NULL;
    if (status.mount_secondary_littlefs) {
        *secondary = JS_NewObject(ctx);
        if (JS_IsException(*secondary) ||
            !esp32_mquickjs_set_property_ref(ctx, secondary, "partition",
                                             JS_NewString(ctx, status.secondary_partition)) ||
            !esp32_mquickjs_set_property_ref(ctx, secondary, "root",
                                             JS_NewString(ctx, status.secondary_root)) ||
            !esp32_mquickjs_set_property_ref(ctx, secondary, "required",
                                             JS_NewBool(status.require_secondary_littlefs))) {
            goto fail;
        }
    }
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "root",
                                         JS_NewString(ctx, status.fs_root)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "mount",
                                         JS_NewBool(status.mount_littlefs)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "required",
                                         JS_NewBool(status.require_littlefs)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "readOnly",
                                         JS_NewBool(status.littlefs_read_only)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "formatOnMountFail",
            JS_NewBool(status.format_littlefs_on_mount_fail)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "secondary", *secondary)) {
        goto fail;
    }
    JS_PopGCRef(ctx, &secondary_ref);
    return JS_PopGCRef(ctx, &object_ref);

fail:
    JS_PopGCRef(ctx, &secondary_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

JSValue js_sys_runtime_info_control(JSContext *ctx,
                                    JSValue *this_val,
                                    int argc,
                                    JSValue *argv)
{
    esp32_mquickjs_host_status_t status;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);

    (void)this_val;
    (void)argc;
    (void)argv;
    if (!sys_get_host_status(&status)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowInternalError(ctx, "runtime host status is unavailable");
    }
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "restartRuntime",
                                         JS_NewBool(status.restart_runtime_available)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "reboot",
                                         JS_NewBool(status.reboot_available)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "restartTimeoutMs",
            status.restart_runtime_available
                ? JS_NewUint32(ctx, status.restart_timeout_ms)
                : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "restartFailureAction",
            status.restart_runtime_available
                ? JS_NewString(ctx,
                               sys_restart_failure_action_name(status.restart_failure_action))
                : JS_NULL)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

static const char *sys_reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "power-on";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt-watchdog";
    case ESP_RST_TASK_WDT:
        return "task-watchdog";
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep-sleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    case ESP_RST_USB:
        return "usb";
    case ESP_RST_JTAG:
        return "jtag";
    case ESP_RST_EFUSE:
        return "efuse";
    case ESP_RST_PWR_GLITCH:
        return "power-glitch";
    case ESP_RST_CPU_LOCKUP:
        return "cpu-lockup";
    case ESP_RST_UNKNOWN:
    default:
        return "unknown";
    }
}

JSValue js_sys_boot_get(JSContext *ctx,
                        JSValue *this_val,
                        int argc,
                        JSValue *argv,
                        int magic)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    esp32_mquickjs_host_status_t status;

    (void)this_val;
    (void)argc;
    (void)argv;
    switch (magic) {
    case 0:
        return runtime != NULL && runtime->boot_id[0] != '\0'
                   ? JS_NewString(ctx, runtime->boot_id)
                   : JS_NULL;
    case 1:
        return JS_NewInt64(ctx, esp_timer_get_time() / 1000);
    case 2:
        if (!sys_get_host_status(&status)) {
            return JS_ThrowInternalError(ctx, "runtime host status is unavailable");
        }
        return status.software_reason_available
                   ? JS_NewString(ctx, status.software_reason)
                   : JS_NULL;
    default:
        return JS_ThrowInternalError(ctx, "invalid sys.status.boot getter");
    }
}

JSValue js_sys_boot_reset(JSContext *ctx,
                          JSValue *this_val,
                          int argc,
                          JSValue *argv)
{
    esp_reset_reason_t reason = esp_reset_reason();
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);

    (void)this_val;
    (void)argc;
    (void)argv;
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "code",
                                         JS_NewInt32(ctx, (int32_t)reason)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "name",
                                         JS_NewString(ctx, sys_reset_reason_name(reason)))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

typedef struct {
    esp_sleep_source_t source;
    const char *name;
} sys_wakeup_name_t;

JSValue js_sys_boot_wakeup(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv)
{
    static const sys_wakeup_name_t wakeup_names[] = {
        { ESP_SLEEP_WAKEUP_EXT0, "ext0" },
        { ESP_SLEEP_WAKEUP_EXT1, "ext1" },
        { ESP_SLEEP_WAKEUP_TIMER, "timer" },
        { ESP_SLEEP_WAKEUP_TOUCHPAD, "touchpad" },
        { ESP_SLEEP_WAKEUP_ULP, "ulp" },
        { ESP_SLEEP_WAKEUP_GPIO, "gpio" },
        { ESP_SLEEP_WAKEUP_UART0, "uart0" },
        { ESP_SLEEP_WAKEUP_UART1, "uart1" },
        { ESP_SLEEP_WAKEUP_WIFI, "wifi" },
        { ESP_SLEEP_WAKEUP_COCPU, "cocpu" },
        { ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG, "cocpu-trap" },
        { ESP_SLEEP_WAKEUP_BT, "bluetooth" },
    };
    uint32_t mask = esp_sleep_get_wakeup_causes();
    uint32_t known_mask = 0;
    uint32_t index = 0;
    size_t i;
    JSGCRef object_ref;
    JSGCRef names_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *names = JS_PushGCRef(ctx, &names_ref);

    (void)this_val;
    (void)argc;
    (void)argv;
    *object = JS_NewObject(ctx);
    *names = JS_NewArray(ctx, 0);
    if (JS_IsException(*object) || JS_IsException(*names)) {
        goto fail;
    }
    for (i = 0; i < sizeof(wakeup_names) / sizeof(wakeup_names[0]); ++i) {
        uint32_t bit = UINT32_C(1) << (uint32_t)wakeup_names[i].source;

        known_mask |= bit;
        if ((mask & bit) != 0U &&
            JS_IsException(JS_SetPropertyUint32(
                ctx,
                *names,
                index++,
                JS_NewString(ctx, wakeup_names[i].name)))) {
            goto fail;
        }
    }
    if ((mask & ~known_mask) != 0U &&
        JS_IsException(JS_SetPropertyUint32(ctx,
                                            *names,
                                            index,
                                            JS_NewString(ctx, "unknown")))) {
        goto fail;
    }
    if (!esp32_mquickjs_set_property_ref(ctx, object, "mask",
                                         JS_NewUint32(ctx, mask)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "names", *names)) {
        goto fail;
    }
    JS_PopGCRef(ctx, &names_ref);
    return JS_PopGCRef(ctx, &object_ref);

fail:
    JS_PopGCRef(ctx, &names_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

JSValue js_sys_cpu_frequency(JSContext *ctx,
                             JSValue *this_val,
                             int argc,
                             JSValue *argv)
{
    uint32_t frequency_hz = 0;

    (void)this_val;
    (void)argc;
    (void)argv;
    if (esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU,
                                     ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                     &frequency_hz) != ESP_OK) {
        return JS_NULL;
    }
    return JS_NewUint32(ctx, frequency_hz);
}

static JSValue sys_heap_status(JSContext *ctx, uint32_t capabilities)
{
    multi_heap_info_t info;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);

    memset(&info, 0, sizeof(info));
    heap_caps_get_info(&info, capabilities);
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "totalBytes",
            JS_NewUint32(ctx, (uint32_t)(info.total_free_bytes +
                                         info.total_allocated_bytes))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "freeBytes",
                                         JS_NewUint32(ctx, (uint32_t)info.total_free_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "allocatedBytes",
            JS_NewUint32(ctx, (uint32_t)info.total_allocated_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "minimumFreeBytes",
            JS_NewUint32(ctx, (uint32_t)info.minimum_free_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "largestFreeBlockBytes",
            JS_NewUint32(ctx, (uint32_t)info.largest_free_block)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "allocatedBlocks",
                                         JS_NewUint32(ctx, (uint32_t)info.allocated_blocks)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "freeBlocks",
                                         JS_NewUint32(ctx, (uint32_t)info.free_blocks)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "totalBlocks",
                                         JS_NewUint32(ctx, (uint32_t)info.total_blocks))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_sys_memory_get(JSContext *ctx,
                          JSValue *this_val,
                          int argc,
                          JSValue *argv,
                          int magic)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    switch (magic) {
    case 0:
        return sys_heap_status(ctx, MALLOC_CAP_DEFAULT);
    case 1:
        return sys_heap_status(ctx, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    case 2:
        return sys_heap_status(ctx, MALLOC_CAP_DMA);
    case 3:
#ifdef CONFIG_SPIRAM
        if (esp_psram_is_initialized()) {
            return sys_heap_status(ctx, MALLOC_CAP_SPIRAM);
        }
#endif
        return JS_NULL;
    default:
        return JS_ThrowInternalError(ctx, "invalid sys.status.memory getter");
    }
}

JSValue js_sys_memory_manager(JSContext *ctx,
                              JSValue *this_val,
                              int argc,
                              JSValue *argv)
{
    esp32_mquickjs_memory_status_t status;
    JSGCRef object_ref;
    JSGCRef allocations_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *allocations = JS_PushGCRef(ctx, &allocations_ref);
    size_t index;

    (void)this_val;
    (void)argc;
    (void)argv;
    esp32_mquickjs_memory_get_status(&status);
    *object = JS_NewObject(ctx);
    *allocations = JS_NewArray(ctx, 0);
    if (JS_IsException(*object) || JS_IsException(*allocations)) {
        goto fail;
    }
    for (index = 0; index < status.allocation_count; ++index) {
        const esp32_mquickjs_memory_owner_entry_t *allocation =
            &status.allocations[index];
        JSGCRef allocation_ref;
        JSValue *allocation_object = JS_PushGCRef(ctx, &allocation_ref);

        *allocation_object = JS_NewObject(ctx);
        if (JS_IsException(*allocation_object) ||
            !esp32_mquickjs_set_property_ref(
                ctx, allocation_object, "owner",
                JS_NewString(ctx, allocation->owner)) ||
            !esp32_mquickjs_set_property_ref(
                ctx, allocation_object, "class",
                JS_NewString(
                    ctx, esp32_mquickjs_memory_class_name(
                             (esp32_mquickjs_memory_class_t)
                                 allocation->memory_class))) ||
            !esp32_mquickjs_set_property_ref(
                ctx, allocation_object, "region",
                JS_NewString(
                    ctx, esp32_mquickjs_memory_region_name(
                             allocation->region))) ||
            !esp32_mquickjs_set_property_ref(
                ctx, allocation_object, "bytes",
                JS_NewUint32(ctx, (uint32_t)allocation->bytes)) ||
            !esp32_mquickjs_set_property_ref(
                ctx, allocation_object, "blocks",
                JS_NewUint32(ctx, allocation->blocks))) {
            JS_PopGCRef(ctx, &allocation_ref);
            goto fail;
        }
        if (JS_IsException(JS_SetPropertyUint32(
                ctx, *allocations, (uint32_t)index,
                *allocation_object))) {
            JS_PopGCRef(ctx, &allocation_ref);
            goto fail;
        }
        JS_PopGCRef(ctx, &allocation_ref);
    }
    if (
        !esp32_mquickjs_set_property_ref(
            ctx, object, "pressure",
            JS_NewString(ctx,
                         esp32_mquickjs_memory_pressure_name(status.pressure))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "internalReserveBytes",
            JS_NewUint32(ctx, (uint32_t)status.internal_reserve_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "dmaLargestReserveBytes",
            JS_NewUint32(ctx, (uint32_t)status.dma_largest_reserve_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "managedInternalBytes",
            JS_NewUint32(ctx, (uint32_t)status.managed_internal_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "managedPsramBytes",
            JS_NewUint32(ctx, (uint32_t)status.managed_psram_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "pinnedBytes",
            JS_NewUint32(ctx, (uint32_t)status.pinned_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "driverPinnedBytes",
            JS_NewUint32(ctx, (uint32_t)status.driver_pinned_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "stagingPinnedBytes",
            JS_NewUint32(ctx, (uint32_t)status.staging_pinned_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "dmaStagingPools",
            JS_NewUint32(ctx, status.dma_staging_pools)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "pendingDmaReservationBytes",
            JS_NewUint32(
                ctx, (uint32_t)status.pending_dma_reservation_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "movableIdleBytes",
            JS_NewUint32(ctx, (uint32_t)status.movable_idle_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "migrationCount",
            JS_NewUint32(ctx, status.migration_count)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "migrationBytes",
            JS_NewUint32(ctx, (uint32_t)status.migration_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "evictionCount",
            JS_NewUint32(ctx, status.eviction_count)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "allocationFailures",
            JS_NewUint32(ctx, status.allocation_failures)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "allocations", *allocations)) {
        goto fail;
    }
    JS_PopGCRef(ctx, &allocations_ref);
    return JS_PopGCRef(ctx, &object_ref);

fail:
    JS_PopGCRef(ctx, &allocations_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

JSValue js_sys_rtos_get(JSContext *ctx,
                        JSValue *this_val,
                        int argc,
                        JSValue *argv,
                        int magic)
{
    BaseType_t scheduler_state;

    (void)this_val;
    (void)argc;
    (void)argv;
    switch (magic) {
    case 0:
        return JS_NewString(ctx, "FreeRTOS");
    case 1:
        scheduler_state = xTaskGetSchedulerState();
        if (scheduler_state == taskSCHEDULER_RUNNING) {
            return JS_NewString(ctx, "running");
        }
        if (scheduler_state == taskSCHEDULER_SUSPENDED) {
            return JS_NewString(ctx, "suspended");
        }
        return JS_NewString(ctx, "not-started");
    case 2:
        return JS_NewUint32(ctx, configTICK_RATE_HZ);
    case 3:
        return JS_NewUint32(ctx, uxTaskGetNumberOfTasks());
    case 4:
#if defined(CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT) && CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT
        return JS_TRUE;
#else
        return JS_FALSE;
#endif
    case 5:
#if defined(CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT) && CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT
        return JS_NewUint32(ctx, CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT_MAX);
#else
        return JS_NewInt32(ctx, 0);
#endif
    default:
        return JS_ThrowInternalError(ctx, "invalid sys.status.rtos getter");
    }
}

JSValue js_sys_rtos_runtime_task(JSContext *ctx,
                                 JSValue *this_val,
                                 int argc,
                                 JSValue *argv)
{
    esp32_mquickjs_host_status_t status;
    const char *name = pcTaskGetName(NULL);
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);

    (void)this_val;
    (void)argc;
    (void)argv;
    if (!sys_get_host_status(&status)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowInternalError(ctx, "runtime host status is unavailable");
    }
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "name",
                                         JS_NewString(ctx, name != NULL ? name : "")) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "priority",
                                         JS_NewUint32(ctx, uxTaskPriorityGet(NULL))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "currentCore",
                                         JS_NewInt32(ctx, portGET_CORE_ID())) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "stackSizeBytes",
            status.managed ? JS_NewUint32(ctx, status.task_stack_size) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "stackHighWaterMarkBytes",
            JS_NewUint32(ctx, uxTaskGetStackHighWaterMark(NULL))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "watchdogEnabled",
                                         JS_NewBool(status.task_watchdog_enabled)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "watchdogRegistered",
            JS_NewBool(esp_task_wdt_status(NULL) == ESP_OK))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

static JSValue sys_pending_control(JSContext *ctx,
                                   const esp32_mquickjs_host_status_t *status)
{
    JSGCRef object_ref;
    JSValue *object;

    if (status == NULL || !status->pending_control) {
        return JS_NULL;
    }
    object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "action",
            JS_NewString(ctx, sys_control_action_name(status->pending_action))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "reason",
                                         JS_NewString(ctx, status->pending_reason)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "requestedAtMs",
            JS_NewInt64(ctx, (int64_t)status->pending_requested_at_ms)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "dueAtMs",
                                         JS_NewInt64(ctx, (int64_t)status->pending_due_at_ms))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_sys_runtime_status_get(JSContext *ctx,
                                  JSValue *this_val,
                                  int argc,
                                  JSValue *argv,
                                  int magic)
{
    esp32_mquickjs_host_status_t status;
    uint64_t now_us = (uint64_t)esp_timer_get_time();

    (void)this_val;
    (void)argc;
    (void)argv;
    if (!sys_get_host_status(&status)) {
        return JS_ThrowInternalError(ctx, "runtime host status is unavailable");
    }
    switch (magic) {
    case 0:
        return JS_NewString(ctx, sys_runtime_state_name(status.state));
    case 1:
        return JS_NewUint32(ctx, status.generation);
    case 2:
        return JS_NewInt64(ctx,
                           status.generation_started_us <= now_us
                               ? (int64_t)((now_us - status.generation_started_us) / 1000ULL)
                               : 0);
    case 3:
        return JS_NewUint32(ctx, status.restart_count);
    case 4:
        return status.last_restart_reason[0] != '\0'
                   ? JS_NewString(ctx, status.last_restart_reason)
                   : JS_NULL;
    case 5:
        return sys_pending_control(ctx, &status);
    default:
        return JS_ThrowInternalError(ctx, "invalid sys.status.runtime getter");
    }
}

JSValue js_sys_runtime_status_filesystem(JSContext *ctx,
                                         JSValue *this_val,
                                         int argc,
                                         JSValue *argv)
{
    esp32_mquickjs_host_status_t status;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);

    (void)this_val;
    (void)argc;
    (void)argv;
    if (!sys_get_host_status(&status)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowInternalError(ctx, "runtime host status is unavailable");
    }
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "root",
                                         JS_NewString(ctx, status.fs_root)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "mounted",
                                         JS_NewBool(status.littlefs_mounted)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "readOnly",
                                         JS_NewBool(status.littlefs_read_only)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "secondaryMounted",
            JS_NewBool(status.secondary_littlefs_mounted))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_sys_runtime_status_watchdog(JSContext *ctx,
                                       JSValue *this_val,
                                       int argc,
                                       JSValue *argv)
{
    esp32_mquickjs_host_status_t status;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    uint64_t now_us = (uint64_t)esp_timer_get_time();
    int64_t heartbeat_age_ms = 0;

    (void)this_val;
    (void)argc;
    (void)argv;
    if (!sys_get_host_status(&status)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowInternalError(ctx, "runtime host status is unavailable");
    }
    if (status.last_outer_heartbeat_us > 0 &&
        status.last_outer_heartbeat_us <= now_us) {
        heartbeat_age_ms = (int64_t)(
            (now_us - status.last_outer_heartbeat_us) / 1000ULL);
    }
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "systemEnabled",
                                         JS_NewBool(status.task_watchdog_enabled)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "systemRegistered",
                                         JS_NewBool(status.task_watchdog_registered)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "jsEnabled",
                                         JS_NewBool(status.js_watchdog_enabled)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "jsRegistered",
                                         JS_NewBool(status.js_watchdog_registered)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "timeoutMs",
                                         JS_NewUint32(ctx, status.watchdog_timeout_ms)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "lastOuterHeartbeatAgeMs",
                                         JS_NewInt64(ctx, heartbeat_age_ms))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_sys_runtime_status_startup(JSContext *ctx,
                                      JSValue *this_val,
                                      int argc,
                                      JSValue *argv)
{
    esp32_mquickjs_host_status_t status;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    const char *phase;

    (void)this_val;
    (void)argc;
    (void)argv;
    if (!sys_get_host_status(&status)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowInternalError(ctx, "runtime host status is unavailable");
    }
    phase = status.safe_mode_active ? "safe-mode" :
            status.startup_pending && !status.startup_stabilizing ? "armed" :
            status.startup_stabilizing ? "stabilizing" : "healthy";
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "phase",
                                         JS_NewString(ctx, phase)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "safeModeActive",
                                         JS_NewBool(status.safe_mode_active)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "safeModeRequested",
                                         JS_NewBool(status.safe_mode_requested)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "failureCount",
                                         JS_NewUint32(ctx, status.startup_failure_count)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "failureLimit",
                                         JS_NewUint32(ctx, status.startup_failure_limit)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "healthyAfterMs",
                                         JS_NewUint32(ctx, status.startup_healthy_ms)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "lastFailureReason",
            status.last_startup_failure_reason[0] != '\0'
                ? JS_NewString(ctx, status.last_startup_failure_reason)
                : JS_NULL)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_sys_safe_mode_get(JSContext *ctx,
                             JSValue *this_val,
                             int argc,
                             JSValue *argv)
{
    esp32_mquickjs_host_status_t status;

    (void)this_val;
    (void)argc;
    (void)argv;
    if (!sys_get_host_status(&status) || !status.safe_mode_available) {
        return JS_ThrowInternalError(ctx, "sys.safeMode is unavailable");
    }
    return JS_NewBool(status.safe_mode_requested);
}

JSValue js_sys_safe_mode_set(JSContext *ctx,
                             JSValue *this_val,
                             int argc,
                             JSValue *argv)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();

    (void)this_val;
    if (argc != 1 || !JS_IsBool(argv[0])) {
        return JS_ThrowTypeError(ctx, "sys.safeMode expects a boolean");
    }
    if (!esp32_mquickjs_set_safe_mode(runtime, argv[0] == JS_TRUE)) {
        return JS_ThrowInternalError(ctx, "failed to persist sys.safeMode");
    }
    return JS_UNDEFINED;
}

static bool sys_set_resource_object(JSContext *ctx,
                                    JSValue *parent,
                                    const char *name,
                                    JSValue *child)
{
    return !JS_IsException(*child) &&
           esp32_mquickjs_set_property_ref(ctx, parent, name, *child);
}

JSValue js_sys_runtime_status_resources(JSContext *ctx,
                                        JSValue *this_val,
                                        int argc,
                                        JSValue *argv)
{
    esp32_mquickjs_resource_status_t status;
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    JSGCRef resources_ref;
    JSGCRef timers_ref;
    JSGCRef futures_ref;
    JSGCRef event_queues_ref;
    JSGCRef async_pollers_ref;
    JSGCRef orphans_ref;
    JSValue *resources = JS_PushGCRef(ctx, &resources_ref);
    JSValue *timers = JS_PushGCRef(ctx, &timers_ref);
    JSValue *futures = JS_PushGCRef(ctx, &futures_ref);
    JSValue *event_queues = JS_PushGCRef(ctx, &event_queues_ref);
    JSValue *async_pollers = JS_PushGCRef(ctx, &async_pollers_ref);
    JSValue *orphans = JS_PushGCRef(ctx, &orphans_ref);

    (void)this_val;
    (void)argc;
    (void)argv;
    if (!esp32_mquickjs_get_resource_status(runtime, &status)) {
        JS_PopGCRef(ctx, &orphans_ref);
        JS_PopGCRef(ctx, &async_pollers_ref);
        JS_PopGCRef(ctx, &event_queues_ref);
        JS_PopGCRef(ctx, &futures_ref);
        JS_PopGCRef(ctx, &timers_ref);
        JS_PopGCRef(ctx, &resources_ref);
        return JS_ThrowInternalError(ctx, "runtime resource status is unavailable");
    }
    *resources = JS_NewObject(ctx);
    *timers = JS_NewObject(ctx);
    *futures = JS_NewObject(ctx);
    *event_queues = JS_NewObject(ctx);
    *async_pollers = JS_NewObject(ctx);
    *orphans = JS_NewObject(ctx);
    if (JS_IsException(*resources) ||
        !esp32_mquickjs_set_property_ref(ctx, timers, "active",
                                         JS_NewUint32(ctx, status.timers_active)) ||
        !esp32_mquickjs_set_property_ref(ctx, timers, "capacity",
                                         JS_NewUint32(ctx, status.timers_capacity)) ||
        !esp32_mquickjs_set_property_ref(ctx, futures, "queued",
                                         JS_NewUint32(ctx, status.futures_queued)) ||
        !esp32_mquickjs_set_property_ref(ctx, futures, "pending",
                                         JS_NewUint32(ctx, status.futures_pending)) ||
        !esp32_mquickjs_set_property_ref(ctx, futures, "capacity",
                                         JS_NewUint32(ctx, status.futures_capacity)) ||
        !esp32_mquickjs_set_property_ref(ctx, futures, "userCapacity",
                                         JS_NewUint32(ctx, status.futures_user_capacity)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            futures,
            "internalReserve",
            JS_NewUint32(ctx, status.futures_internal_reserve)) ||
        !esp32_mquickjs_set_property_ref(ctx, event_queues, "open",
                                         JS_NewUint32(ctx, status.event_queues_open)) ||
        !esp32_mquickjs_set_property_ref(ctx, event_queues, "dropped",
                                         JS_NewUint32(ctx, status.event_queues_dropped)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            async_pollers,
            "registered",
            JS_NewUint32(ctx, status.async_pollers_registered)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            async_pollers,
            "capacity",
            JS_NewUint32(ctx, status.async_pollers_capacity)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            orphans,
            "pending",
            JS_NewUint32(ctx, status.orphans_pending)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            orphans,
            "capacity",
            JS_NewUint32(ctx, status.orphans_capacity)) ||
        !sys_set_resource_object(ctx, resources, "timers", timers) ||
        !sys_set_resource_object(ctx, resources, "futures", futures) ||
        !sys_set_resource_object(ctx, resources, "eventQueues", event_queues) ||
        !sys_set_resource_object(ctx, resources, "asyncPollers", async_pollers) ||
        !sys_set_resource_object(ctx, resources, "orphans", orphans)) {
        goto fail;
    }
    JS_PopGCRef(ctx, &orphans_ref);
    JS_PopGCRef(ctx, &async_pollers_ref);
    JS_PopGCRef(ctx, &event_queues_ref);
    JS_PopGCRef(ctx, &futures_ref);
    JS_PopGCRef(ctx, &timers_ref);
    return JS_PopGCRef(ctx, &resources_ref);

fail:
    JS_PopGCRef(ctx, &orphans_ref);
    JS_PopGCRef(ctx, &async_pollers_ref);
    JS_PopGCRef(ctx, &event_queues_ref);
    JS_PopGCRef(ctx, &futures_ref);
    JS_PopGCRef(ctx, &timers_ref);
    JS_PopGCRef(ctx, &resources_ref);
    return JS_EXCEPTION;
}

static bool sys_number_to_bounded_u32(JSContext *ctx,
                                      JSValue value,
                                      uint32_t minimum,
                                      uint32_t maximum,
                                      uint32_t *result)
{
    double number;
    uint32_t converted;

    if (!JS_IsNumber(ctx, value) || JS_ToNumber(ctx, &number, value) != 0 ||
        !isfinite(number) || number < (double)minimum || number > (double)maximum) {
        return false;
    }
    converted = (uint32_t)number;
    if ((double)converted != number) {
        return false;
    }
    *result = converted;
    return true;
}

static bool sys_validate_option_keys(JSContext *ctx,
                                     JSValue options,
                                     const char *api_name,
                                     const char *first,
                                     const char *second)
{
    const char *allowed[2] = {first, second};
    return esp32_mquickjs_validate_plain_options(
        ctx, options, api_name, allowed, second != NULL ? 2U : 1U);
}

#if defined(CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT) && CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT
static int sys_compare_tasks(const void *left, const void *right)
{
    const TaskStatus_t *left_task = left;
    const TaskStatus_t *right_task = right;

    if (left_task->xTaskNumber < right_task->xTaskNumber) {
        return -1;
    }
    if (left_task->xTaskNumber > right_task->xTaskNumber) {
        return 1;
    }
    return 0;
}

static const char *sys_task_state_name(eTaskState state)
{
    switch (state) {
    case eRunning:
        return "running";
    case eReady:
        return "ready";
    case eBlocked:
        return "blocked";
    case eSuspended:
        return "suspended";
    case eDeleted:
        return "deleted";
    case eInvalid:
    default:
        return "invalid";
    }
}

static JSValue sys_task_core(JSContext *ctx, const TaskStatus_t *task)
{
#if CONFIG_FREERTOS_SMP
#if configNUMBER_OF_CORES == 1
    (void)task;
    return JS_NewInt32(ctx, 0);
#elif defined(configUSE_CORE_AFFINITY) && configUSE_CORE_AFFINITY
    UBaseType_t mask = task->uxCoreAffinityMask;
    BaseType_t core;

    for (core = 0; core < configNUMBER_OF_CORES; ++core) {
        UBaseType_t bit = (UBaseType_t)1U << (UBaseType_t)core;

        if (mask == bit) {
            return JS_NewInt32(ctx, core);
        }
    }
    return JS_NULL;
#else
    (void)task;
    return JS_NULL;
#endif
#elif defined(configTASKLIST_INCLUDE_COREID) && configTASKLIST_INCLUDE_COREID
    if (taskVALID_CORE_ID(task->xCoreID) == pdTRUE) {
        return JS_NewInt32(ctx, task->xCoreID);
    }
    return JS_NULL;
#elif configNUMBER_OF_CORES == 1
    (void)task;
    return JS_NewInt32(ctx, 0);
#else
    (void)task;
    return JS_NULL;
#endif
}

static JSValue sys_make_task(JSContext *ctx, const TaskStatus_t *task)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);

    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "id",
                                         JS_NewUint32(ctx, task->xTaskNumber)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "name",
            JS_NewString(ctx, task->pcTaskName != NULL ? task->pcTaskName : "")) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "state",
            JS_NewString(ctx, sys_task_state_name(task->eCurrentState))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "priority",
                                         JS_NewUint32(ctx, task->uxCurrentPriority)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "basePriority",
                                         JS_NewUint32(ctx, task->uxBasePriority)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "core",
                                         sys_task_core(ctx, task)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "stackHighWaterMarkBytes",
            JS_NewUint32(ctx, task->usStackHighWaterMark))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}
#endif

JSValue js_sys_tasks(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
#if !defined(CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT) || !CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT
    (void)argc;
    (void)argv;
    return JS_ThrowInternalError(ctx, "FreeRTOS task snapshots are not enabled");
#else
    TaskStatus_t *native_tasks = NULL;
    UBaseType_t before_count;
    UBaseType_t captured_count;
    UBaseType_t after_count;
    uint32_t limit;
    uint32_t return_count;
    uint32_t i;
    JSGCRef result_ref;
    JSGCRef tasks_ref;
    JSValue *result;
    JSValue *tasks;

    limit = CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT_MAX;
    if (argc > 0 && !JS_IsUndefined(argv[0])) {
        JSGCRef property_ref;
        JSValue *property;

        if (JS_GetClassID(ctx, argv[0]) != JS_CLASS_OBJECT) {
            return JS_ThrowTypeError(ctx, "sys.tasks(options) expects an object");
        }
        if (!sys_validate_option_keys(ctx, argv[0], "sys.tasks", "limit", NULL)) {
            return JS_EXCEPTION;
        }
        property = JS_PushGCRef(ctx, &property_ref);
        *property = JS_GetPropertyStr(ctx, argv[0], "limit");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) &&
            !sys_number_to_bounded_u32(
                ctx,
                *property,
                1U,
                CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT_MAX,
                &limit)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowRangeError(
                ctx,
                "sys.tasks option 'limit' expects an integer from 1 through %u",
                (unsigned)CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT_MAX);
        }
        JS_PopGCRef(ctx, &property_ref);
    }
    before_count = uxTaskGetNumberOfTasks();
    if (before_count > CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT_MAX) {
        return JS_ThrowInternalError(
            ctx,
            "FreeRTOS task count exceeds the configured snapshot limit");
    }
    native_tasks = heap_caps_calloc(CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT_MAX,
                                    sizeof(*native_tasks),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (native_tasks == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    captured_count = uxTaskGetSystemState(
        native_tasks,
        CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT_MAX,
        NULL);
    after_count = uxTaskGetNumberOfTasks();
    if (after_count > CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT_MAX ||
        captured_count > CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT_MAX) {
        heap_caps_free(native_tasks);
        return JS_ThrowInternalError(
            ctx,
            "FreeRTOS task count exceeds the configured snapshot limit");
    }
    qsort(native_tasks, captured_count, sizeof(*native_tasks), sys_compare_tasks);
    return_count = captured_count < limit ? captured_count : limit;
    result = JS_PushGCRef(ctx, &result_ref);
    tasks = JS_PushGCRef(ctx, &tasks_ref);
    *result = JS_NewObject(ctx);
    *tasks = JS_NewArray(ctx, 0);
    if (JS_IsException(*result) || JS_IsException(*tasks)) {
        goto task_fail;
    }
    for (i = 0; i < return_count; ++i) {
        JSValue task = sys_make_task(ctx, &native_tasks[i]);

        if (JS_IsException(task) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *tasks, i, task))) {
            goto task_fail;
        }
    }
    if (!esp32_mquickjs_set_property_ref(ctx, result, "total",
                                         JS_NewUint32(ctx, captured_count)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "truncated",
                                         JS_NewBool(return_count < captured_count)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "tasks", *tasks)) {
        goto task_fail;
    }
    heap_caps_free(native_tasks);
    JS_PopGCRef(ctx, &tasks_ref);
    return JS_PopGCRef(ctx, &result_ref);

task_fail:
    heap_caps_free(native_tasks);
    JS_PopGCRef(ctx, &tasks_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
#endif
}

typedef struct {
    char reason[ESP32_MQUICKJS_CONTROL_REASON_MAX + 1U];
    uint32_t delay_ms;
} sys_control_options_t;

static bool sys_parse_control_options(JSContext *ctx,
                                      const char *api_name,
                                      int argc,
                                      JSValue *argv,
                                      sys_control_options_t *options)
{
    JSGCRef reason_ref;
    JSGCRef delay_ref;
    JSValue *reason;
    JSValue *delay;

    snprintf(options->reason, sizeof(options->reason), "%s", "javascript");
    options->delay_ms = 0;
    if (argc == 0 || JS_IsUndefined(argv[0])) {
        return true;
    }
    if (JS_GetClassID(ctx, argv[0]) != JS_CLASS_OBJECT) {
        JS_ThrowTypeError(ctx, "%s(options) expects an object", api_name);
        return false;
    }
    if (!sys_validate_option_keys(ctx, argv[0], api_name, "reason", "delayMs")) {
        return false;
    }
    reason = JS_PushGCRef(ctx, &reason_ref);
    delay = JS_PushGCRef(ctx, &delay_ref);
    *reason = JS_GetPropertyStr(ctx, argv[0], "reason");
    *delay = JS_GetPropertyStr(ctx, argv[0], "delayMs");
    if (JS_IsException(*reason) || JS_IsException(*delay)) {
        goto fail;
    }
    if (!JS_IsUndefined(*reason)) {
        JSCStringBuf reason_buf;
        size_t reason_length = 0;
        const char *reason_text;
        size_t i;

        if (!JS_IsString(ctx, *reason)) {
            JS_ThrowTypeError(ctx, "%s option 'reason' expects a string", api_name);
            goto fail;
        }
        reason_text = JS_ToCStringLen(ctx, &reason_length, *reason, &reason_buf);
        if (reason_text == NULL) {
            goto fail;
        }
        if (reason_length == 0 || reason_length > ESP32_MQUICKJS_CONTROL_REASON_MAX) {
            JS_ThrowRangeError(ctx,
                               "%s option 'reason' expects 1..%u UTF-8 bytes",
                               api_name,
                               (unsigned)ESP32_MQUICKJS_CONTROL_REASON_MAX);
            goto fail;
        }
        for (i = 0; i < reason_length; ++i) {
            unsigned char byte = (unsigned char)reason_text[i];

            if (byte < 0x20U || byte == 0x7fU) {
                JS_ThrowRangeError(ctx,
                                   "%s option 'reason' must not contain ASCII control characters",
                                   api_name);
                goto fail;
            }
        }
        memcpy(options->reason, reason_text, reason_length);
        options->reason[reason_length] = '\0';
    }
    if (!JS_IsUndefined(*delay) &&
        !sys_number_to_bounded_u32(ctx, *delay, 0U, 60000U, &options->delay_ms)) {
        JS_ThrowRangeError(ctx,
                           "%s option 'delayMs' expects an integer from 0 through 60000",
                           api_name);
        goto fail;
    }
    JS_PopGCRef(ctx, &delay_ref);
    JS_PopGCRef(ctx, &reason_ref);
    return true;

fail:
    JS_PopGCRef(ctx, &delay_ref);
    JS_PopGCRef(ctx, &reason_ref);
    return false;
}

static JSValue sys_control_receipt(JSContext *ctx,
                                   esp32_mquickjs_control_action_t action,
                                   const char *reason,
                                   const esp32_mquickjs_control_receipt_t *receipt)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);

    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "action",
            JS_NewString(ctx, sys_control_action_name(action))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "reason",
                                         JS_NewString(ctx, reason)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "generation",
                                         JS_NewUint32(ctx, receipt->generation)) ||
        !esp32_mquickjs_set_property_ref(
            ctx,
            object,
            "requestedAtMs",
            JS_NewInt64(ctx, (int64_t)receipt->requested_at_ms)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "dueAtMs",
                                         JS_NewInt64(ctx, (int64_t)receipt->due_at_ms))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

static JSValue sys_request_control(JSContext *ctx,
                                   const char *api_name,
                                   esp32_mquickjs_control_action_t action,
                                   int argc,
                                   JSValue *argv)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    esp32_mquickjs_control_receipt_t receipt;
    esp32_mquickjs_control_result_t result;
    sys_control_options_t options;

    if (!sys_parse_control_options(ctx, api_name, argc, argv, &options)) {
        return JS_EXCEPTION;
    }
    if (!esp32_mquickjs_request_system_control(runtime,
                                                action,
                                                options.reason,
                                                options.delay_ms,
                                                &receipt,
                                                &result)) {
        switch (result) {
        case ESP32_MQUICKJS_CONTROL_ALREADY_PENDING:
            return JS_ThrowInternalError(ctx, "system control already pending");
        case ESP32_MQUICKJS_CONTROL_INVALID_STATE:
            return JS_ThrowInternalError(ctx, "%s is unavailable in the current runtime state",
                                         api_name);
        case ESP32_MQUICKJS_CONTROL_UNAVAILABLE:
        default:
            return JS_ThrowInternalError(ctx, "%s is unavailable", api_name);
        }
    }
    return sys_control_receipt(ctx, action, options.reason, &receipt);
}

JSValue js_sys_restart_runtime(JSContext *ctx,
                               JSValue *this_val,
                               int argc,
                               JSValue *argv)
{
    (void)this_val;
    return sys_request_control(ctx,
                               "sys.restartRuntime",
                               ESP32_MQUICKJS_CONTROL_RESTART_RUNTIME,
                               argc,
                               argv);
}

JSValue js_sys_reboot(JSContext *ctx,
                      JSValue *this_val,
                      int argc,
                      JSValue *argv)
{
    (void)this_val;
    return sys_request_control(ctx,
                               "sys.reboot",
                               ESP32_MQUICKJS_CONTROL_REBOOT,
                               argc,
                               argv);
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
    uint32_t timeout_ms;
    JSValue result;

    (void)this_val;
    if (argc < 2 || !esp32_mquickjs_value_to_bounded_u32(
            ctx, argv[0], 1U, ESP32_MQUICKJS_MAX_SCOPED_TIMEOUT_MS,
            &timeout_ms)) {
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
                            ((uint64_t)timeout_ms * 1000ULL);
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
