#include "mquickjs_priv.h"
#define CONFIG_ESP32_MQUICKJS_FEATURE_FS 1
#include "utils/esp32_mquickjs_fs_path.h"
#define ESP32_MQUICKJS_MAX_LITTLEFS_MOUNTS 4U
#define ESP32_MQUICKJS_FS_CHANGE_QUEUE_LEN 8U
#define ESP32_MQUICKJS_FS_CHANGE_QUEUE_MAX_LEN 64U
#define ESP32_MQUICKJS_MAX_SCRIPT_PATH 256U
#define ESP_OK 0
#define ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST 0
#define portMAX_DELAY 0
#define xSemaphoreTake(lock, timeout) ((void)(lock), (void)(timeout))
#define xSemaphoreGive(lock) ((void)(lock))
typedef int esp_err_t;
typedef struct { void *fs_state; } esp32_mquickjs_runtime_t;
typedef struct { int unused; } esp32_mquickjs_event_queue_t;
typedef struct {
    bool active, read_only;
    char partition_label[17], base_path[64];
} esp32_mquickjs_littlefs_mount_t;
static esp32_mquickjs_littlefs_mount_t s_littlefs_mounts[4];
typedef enum {
    ESP32_MQUICKJS_FS_CHANGE_WRITE, ESP32_MQUICKJS_FS_CHANGE_REMOVE,
    ESP32_MQUICKJS_FS_CHANGE_RENAME, ESP32_MQUICKJS_FS_CHANGE_MKDIR
} esp32_mquickjs_fs_change_kind_t;
typedef struct {
    esp32_mquickjs_fs_change_kind_t kind;
    uint32_t sequence; int64_t timestamp_us;
    char path[256], to_path[256];
} esp32_mquickjs_fs_change_event_t;
typedef struct esp32_mquickjs_fs_change_source {
    struct esp32_mquickjs_fs_runtime *owner;
    esp32_mquickjs_event_queue_t *changes;
    uint32_t event_sequence;
    char path[256];
    struct esp32_mquickjs_fs_change_source *next;
} esp32_mquickjs_fs_change_source_t;
typedef struct esp32_mquickjs_fs_runtime {
    void *lock; esp32_mquickjs_fs_change_source_t *sources;
} esp32_mquickjs_fs_runtime_t;
static esp32_mquickjs_runtime_t runtime;
static esp32_mquickjs_event_queue_t dummy_queue;
static uint32_t captured_capacity;
static esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void) { return &runtime; }
static esp_err_t esp_littlefs_info(const char *partition, size_t *total, size_t *used)
{
    *total = strcmp(partition, "storage") == 0 ? 512 : 4096;
    *used = 128;
    return ESP_OK;
}
static const char *esp_err_to_name(esp_err_t error) { (void)error; return "SDK_ERROR"; }
static JSValue esp32_mquickjs_event_queue_new(JSContext *ctx, esp32_mquickjs_runtime_t *owner,
    size_t size, uint32_t capacity, int policy,
    JSValue (*convert)(JSContext *, const void *, void *), void *convert_opaque,
    void (*closed)(void *), void *source)
{
    (void)owner; (void)policy; (void)convert; (void)convert_opaque; (void)closed; (void)source;
    assert(size == sizeof(esp32_mquickjs_fs_change_event_t));
    captured_capacity = capacity;
    return JS_NewObject(ctx);
}
static esp32_mquickjs_event_queue_t *esp32_mquickjs_event_queue_from_value(JSContext *ctx, JSValue value)
{
    (void)ctx; (void)value; return &dummy_queue;
}
