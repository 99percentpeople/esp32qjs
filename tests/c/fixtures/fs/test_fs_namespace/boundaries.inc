#include "utils/esp32_mquickjs_fs_path.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ESP32_MQUICKJS_MAX_LITTLEFS_MOUNTS 4U
#define ESP32_MQUICKJS_FS_ROOT_MAX 64U
#define ESP32_MQUICKJS_MAX_SCRIPT_PATH 256U
#define ESP32_MQUICKJS_FS_FUTURE_MAX_ENTRIES 64U
#define ESP32_MQUICKJS_MEMORY_EXTERNAL 0
#define JS_EXCEPTION (-1)
typedef int JSValue;
typedef struct { int unused; } JSContext;
typedef struct { char load_root[64]; uint16_t load_root_depth; void *fs_state; } esp32_mquickjs_runtime_t;
typedef struct { char root[64]; uint16_t depth; } fs_load_context_t;
typedef struct { bool active; bool read_only; char base_path[64]; } esp32_mquickjs_littlefs_mount_t;
static esp32_mquickjs_littlefs_mount_t s_littlefs_mounts[4];
typedef struct { char path[256]; struct stat stat_value; } fs_future_entry_t;
typedef struct {
    char path[256]; fs_future_entry_t *entries; size_t entry_count; int error_number;
} esp32_mquickjs_future_driver_state_t;
static char sandbox_root[256];
static void *esp32_mquickjs_memory_payload_alloc(const char *tag, size_t size, int policy)
{ (void)tag; (void)policy; return malloc(size); }
static void *esp32_mquickjs_memory_payload_realloc(const char *tag, void *old, size_t size, int policy)
{ (void)tag; (void)policy; return realloc(old, size); }
#define esp32_mquickjs_memory_payload_free free
static JSValue JS_ThrowInternalError(JSContext *ctx, const char *format, ...)
{ (void)ctx; (void)format; return JS_EXCEPTION; }
#define JS_ThrowTypeError JS_ThrowInternalError
#define JS_ThrowReferenceError JS_ThrowInternalError
static JSValue esp32_mquickjs_eval(JSContext *, esp32_mquickjs_runtime_t *, const char *, const char *, int);

/* Route the same namespace to separate temporary host directories. No device I/O. */
static const char *host_path(const char *path)
{
    static char result[1024];
    bool system = esp32_mquickjs_fs_path_contains("/framework", path);
    snprintf(result, sizeof(result), "%s/%s%s", sandbox_root,
             system ? "system" : "user", system ? path + strlen("/framework") : path);
    return result;
}
static int host_stat(const char *path, struct stat *st) { return stat(host_path(path), st); }
static DIR *host_opendir(const char *path) { return opendir(host_path(path)); }
static FILE *host_fopen(const char *path, const char *mode) { return fopen(host_path(path), mode); }
#define stat(path, st) host_stat(path, st)
#define opendir(path) host_opendir(path)
#define fopen(path, mode) host_fopen(path, mode)

/* A bounded native queue boundary records production watch routing. */
typedef enum {
    ESP32_MQUICKJS_FS_CHANGE_WRITE, ESP32_MQUICKJS_FS_CHANGE_REMOVE,
    ESP32_MQUICKJS_FS_CHANGE_RENAME, ESP32_MQUICKJS_FS_CHANGE_MKDIR
} esp32_mquickjs_fs_change_kind_t;
typedef struct {
    esp32_mquickjs_fs_change_kind_t kind;
    uint32_t sequence;
    int64_t timestamp_us;
    char path[256], to_path[256];
} esp32_mquickjs_fs_change_event_t;
typedef struct {
    esp32_mquickjs_fs_change_event_t events[8]; size_t count;
} esp32_mquickjs_event_queue_t;
typedef struct esp32_mquickjs_fs_change_source {
    void *owner;
    esp32_mquickjs_event_queue_t *changes;
    uint32_t event_sequence;
    char path[256];
    struct esp32_mquickjs_fs_change_source *next;
} esp32_mquickjs_fs_change_source_t;
typedef struct {
    void *lock; esp32_mquickjs_fs_change_source_t *sources;
} esp32_mquickjs_fs_runtime_t;
static esp32_mquickjs_runtime_t *active_runtime;
static esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void) { return active_runtime; }
static int64_t esp_timer_get_time(void) { return 1000; }
#define portMAX_DELAY 0
#define xSemaphoreTake(lock, timeout) ((void)(lock), (void)(timeout))
#define xSemaphoreGive(lock) ((void)(lock))
static bool esp32_mquickjs_event_queue_send(esp32_mquickjs_event_queue_t *queue, const void *event)
{
    assert(queue->count < 8);
    queue->events[queue->count++] = *(const esp32_mquickjs_fs_change_event_t *)event;
    return true;
}
