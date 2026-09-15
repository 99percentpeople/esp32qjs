#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 1
#define NVS_READWRITE 1
#define ESP32QJS_BOOT_GUARD_VERSION 1
#define ESP32QJS_BOOT_GUARD_NAMESPACE "qjs_rt"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define CONFIG_ESP32_MQUICKJS_FEATURE_FS 1

typedef int esp_err_t;
typedef int nvs_handle_t;
typedef enum {
    ESP_RST_POWERON, ESP_RST_SW, ESP_RST_USB, ESP_RST_EXT, ESP_RST_PANIC,
    ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_CPU_LOCKUP
} esp_reset_reason_t;
typedef enum {
    ESP32_MQUICKJS_SAFE_MODE_NORMAL = 0,
    ESP32_MQUICKJS_SAFE_MODE_SOFT = 1,
    ESP32_MQUICKJS_SAFE_MODE_HARD = 2,
} esp32_mquickjs_safe_mode_t;
typedef struct {
    struct {
        bool startup_guard, autorun_startup_script;
        uint32_t startup_failure_limit, startup_healthy_ms;
    } config;
    bool boot_guard_ready, failure_reboot_pending, startup_pending, startup_stabilizing;
    esp32_mquickjs_safe_mode_t safe_mode;
    bool littlefs_mounted;
    void *ctx;
    int engine;
    char startup_script[32];
    uint8_t startup_failure_count;
    uint64_t startup_returned_us;
    char last_startup_failure_reason[65];
} esp32qjs_runtime_t;

static struct {
    uint8_t version, failures, failed;
    char reason[65];
} persisted;
static esp_reset_reason_t reset_reason;
static uint64_t now_us;
static uint8_t *nvs_byte(const char *key)
{
    if (!strcmp(key, "version")) return &persisted.version;
    if (!strcmp(key, "failures")) return &persisted.failures;
    assert(!strcmp(key, "failed"));
    return &persisted.failed;
}
static esp_err_t nvs_flash_init(void) { return ESP_OK; }
static esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *handle)
{
    assert(!strcmp(ns, "qjs_rt"));
    *handle = 1;
    return ESP_OK;
}
static esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *value)
{
    *value = *nvs_byte(key);
    return !strcmp(key, "version") && !*value ? ESP_ERR_NVS_NOT_FOUND : ESP_OK;
}
static esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value)
{
    *nvs_byte(key) = value;
    return ESP_OK;
}
static esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *value, size_t *size)
{
    assert(!strcmp(key, "reason"));
    if (!persisted.reason[0]) return ESP_ERR_NVS_NOT_FOUND;
    assert(*size > strlen(persisted.reason));
    strcpy(value, persisted.reason);
    return ESP_OK;
}
static esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value)
{
    assert(!strcmp(key, "reason"));
    strcpy(persisted.reason, value);
    return ESP_OK;
}
static esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    assert(!strcmp(key, "reason"));
    persisted.reason[0] = '\0';
    return ESP_OK;
}
static esp_err_t nvs_erase_all(nvs_handle_t h)
{
    memset(&persisted, 0, sizeof(persisted));
    return ESP_OK;
}
static esp_err_t nvs_commit(nvs_handle_t h) { return ESP_OK; }
static void nvs_close(nvs_handle_t h) { }
static esp_reset_reason_t esp_reset_reason(void) { return reset_reason; }
static int64_t esp_timer_get_time(void) { return now_us; }

typedef int JSValue;
static unsigned startup_loads;
static JSValue esp32_mquickjs_load_startup_from_active_fs(void *ctx, int *engine,
                                                        const char *path)
{
    assert(ctx && !strcmp(path, "index.js"));
    startup_loads++;
    return 0;
}
static bool JS_IsException(JSValue value) { return value == -1; }
static void esp32_mquickjs_print_exception(void *ctx) { }
static void runtime_store_software_reason(const char *reason) { }
static void esp_restart(void) { assert(!"Unexpected reboot in successful loader fixture"); }
