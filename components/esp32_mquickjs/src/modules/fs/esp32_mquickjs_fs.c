#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_FS

#include "esp32_mquickjs_fs.h"
#include "esp32_mquickjs_memory.h"

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_fs_events.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_fs_atomic_write.h"
#include "esp32_mquickjs_options.h"
#include "utils/esp32_mquickjs_fs_path.h"
#include "esp32_mquickjs_stream.h"
#include "mquickjs_priv.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "esp32qjs";

#define ESP32_MQUICKJS_MAX_LITTLEFS_MOUNTS 4U
#define ESP32_MQUICKJS_PARTITION_LABEL_MAX 17U
#define ESP32_MQUICKJS_FS_CHANGE_QUEUE_LEN 8U
#define ESP32_MQUICKJS_FS_CHANGE_QUEUE_MAX_LEN 64U
#define ESP32_MQUICKJS_FS_ATOMIC_TEMP_ATTEMPTS 32U

typedef struct {
    bool active;
    bool read_only;
    char partition_label[ESP32_MQUICKJS_PARTITION_LABEL_MAX];
    char base_path[ESP32_MQUICKJS_FS_ROOT_MAX];
} esp32_mquickjs_littlefs_mount_t;

typedef struct {
    esp32_mquickjs_fs_change_kind_t kind;
    uint32_t sequence;
    int64_t timestamp_us;
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    char to_path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
} esp32_mquickjs_fs_change_event_t;

typedef struct esp32_mquickjs_fs_change_source {
    struct esp32_mquickjs_fs_runtime *owner;
    esp32_mquickjs_event_queue_t *changes;
    uint32_t event_sequence;
    char root[ESP32_MQUICKJS_FS_ROOT_MAX];
    struct esp32_mquickjs_fs_change_source *next;
} esp32_mquickjs_fs_change_source_t;

typedef struct esp32_mquickjs_fs_runtime {
    SemaphoreHandle_t lock;
    esp32_mquickjs_fs_change_source_t *sources;
} esp32_mquickjs_fs_runtime_t;

typedef struct {
    char root[ESP32_MQUICKJS_FS_ROOT_MAX];
} esp32_mquickjs_fs_volume_t;

static bool s_littlefs_mounted;
static _Atomic uint32_t s_fs_atomic_temp_counter;
static esp32_mquickjs_littlefs_mount_t
    s_littlefs_mounts[ESP32_MQUICKJS_MAX_LITTLEFS_MOUNTS];

static esp32_mquickjs_fs_runtime_t *fs_runtime(
    esp32_mquickjs_runtime_t *runtime)
{
    return runtime != NULL ? runtime->fs_state : NULL;
}

static const char *fs_change_kind_name(esp32_mquickjs_fs_change_kind_t kind)
{
    switch (kind) {
    case ESP32_MQUICKJS_FS_CHANGE_WRITE:
        return "write";
    case ESP32_MQUICKJS_FS_CHANGE_REMOVE:
        return "remove";
    case ESP32_MQUICKJS_FS_CHANGE_RENAME:
        return "rename";
    case ESP32_MQUICKJS_FS_CHANGE_MKDIR:
        return "mkdir";
    default:
        return "change";
    }
}

static bool fs_change_relative_path(const char *root,
                                    const char *path,
                                    char *relative,
                                    size_t relative_size)
{
    size_t root_length;
    const char *suffix;

    if (root == NULL || path == NULL || relative == NULL || relative_size < 2U) {
        return false;
    }
    root_length = strlen(root);
    if (root_length == 0U || strncmp(path, root, root_length) != 0 ||
        (path[root_length] != '\0' && path[root_length] != '/')) {
        return false;
    }
    suffix = path + root_length;
    while (*suffix == '/') {
        suffix++;
    }
    if (*suffix == '\0') {
        suffix = ".";
    }
    if (strlen(suffix) >= relative_size) {
        return false;
    }
    snprintf(relative, relative_size, "%s", suffix);
    return true;
}

static JSValue fs_change_to_js(JSContext *ctx,
                               const void *event_value,
                               void *opaque)
{
    const esp32_mquickjs_fs_change_event_t *event = event_value;
    JSGCRef result_ref;
    JSValue *result;

    (void)opaque;
    if (event == NULL) {
        return JS_NULL;
    }
    result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "sequence", JS_NewUint32(ctx, event->sequence)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "timestampUs", JS_NewInt64(ctx, event->timestamp_us)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "type", JS_NewString(ctx, fs_change_kind_name(event->kind))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "path", JS_NewString(ctx, event->path)) ||
        (event->kind == ESP32_MQUICKJS_FS_CHANGE_RENAME &&
         !esp32_mquickjs_set_property_ref(
             ctx, result, "toPath", JS_NewString(ctx, event->to_path)))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static void fs_change_queue_closed(void *opaque)
{
    esp32_mquickjs_fs_change_source_t *source = opaque;
    esp32_mquickjs_fs_runtime_t *state;
    esp32_mquickjs_fs_change_source_t **cursor;

    if (source == NULL || (state = source->owner) == NULL || state->lock == NULL) {
        return;
    }
    xSemaphoreTake(state->lock, portMAX_DELAY);
    cursor = &state->sources;
    while (*cursor != NULL) {
        if (*cursor == source) {
            *cursor = source->next;
            break;
        }
        cursor = &(*cursor)->next;
    }
    xSemaphoreGive(state->lock);
    heap_caps_free(source);
}

void esp32_mquickjs_fs_notify_change(esp32_mquickjs_fs_change_kind_t kind,
                                     const char *path,
                                     const char *to_path)
{
    esp32_mquickjs_fs_runtime_t *state = fs_runtime(
        esp32_mquickjs_get_active_runtime());
    esp32_mquickjs_fs_change_source_t *source;
    esp32_mquickjs_fs_change_event_t event;
    int64_t timestamp_us;

    if (state == NULL || state->lock == NULL || path == NULL) {
        return;
    }
    timestamp_us = esp_timer_get_time();
    xSemaphoreTake(state->lock, portMAX_DELAY);
    for (source = state->sources; source != NULL; source = source->next) {
        memset(&event, 0, sizeof(event));
        event.kind = kind;
        if (fs_change_relative_path(source->root, path,
                                    event.path, sizeof(event.path)) &&
            (kind != ESP32_MQUICKJS_FS_CHANGE_RENAME ||
             fs_change_relative_path(source->root, to_path,
                                     event.to_path, sizeof(event.to_path)))) {
            event.sequence = ++source->event_sequence;
            if (event.sequence == 0) {
                event.sequence = ++source->event_sequence;
            }
            event.timestamp_us = timestamp_us;
            (void)esp32_mquickjs_event_queue_send(source->changes, &event);
        }
    }
    xSemaphoreGive(state->lock);
}

static esp32_mquickjs_littlefs_mount_t *find_littlefs_mount_by_root(
    const char *base_path)
{
    esp32_mquickjs_littlefs_mount_t *matched = NULL;
    size_t matched_length = 0;
    size_t mount_length;
    size_t i;

    if (base_path == NULL) {
        return NULL;
    }
    for (i = 0; i < ESP32_MQUICKJS_MAX_LITTLEFS_MOUNTS; i++) {
        if (!s_littlefs_mounts[i].active) {
            continue;
        }
        mount_length = strlen(s_littlefs_mounts[i].base_path);
        if (mount_length > matched_length &&
            strncmp(s_littlefs_mounts[i].base_path, base_path, mount_length) == 0 &&
            (base_path[mount_length] == '\0' || base_path[mount_length] == '/')) {
            matched = &s_littlefs_mounts[i];
            matched_length = mount_length;
        }
    }
    return matched;
}

esp32_mquickjs_resource_key_t esp32_mquickjs_fs_resource_key_for_path(
    const char *path)
{
    return find_littlefs_mount_by_root(path);
}

static esp32_mquickjs_littlefs_mount_t *find_littlefs_mount_exact(
    const char *base_path)
{
    size_t i;

    if (base_path == NULL) {
        return NULL;
    }
    for (i = 0; i < ESP32_MQUICKJS_MAX_LITTLEFS_MOUNTS; ++i) {
        if (s_littlefs_mounts[i].active &&
            strcmp(s_littlefs_mounts[i].base_path, base_path) == 0) {
            return &s_littlefs_mounts[i];
        }
    }
    return NULL;
}

static bool remember_littlefs_mount(const char *partition_label,
                                    const char *base_path,
                                    bool read_only)
{
    esp32_mquickjs_littlefs_mount_t *available = NULL;
    size_t i;

    for (i = 0; i < ESP32_MQUICKJS_MAX_LITTLEFS_MOUNTS; i++) {
        if (s_littlefs_mounts[i].active &&
            strcmp(s_littlefs_mounts[i].partition_label, partition_label) == 0) {
            available = &s_littlefs_mounts[i];
            break;
        }
        if (!s_littlefs_mounts[i].active && available == NULL) {
            available = &s_littlefs_mounts[i];
        }
    }
    if (available == NULL) {
        return false;
    }
    snprintf(available->partition_label,
             sizeof(available->partition_label),
             "%s",
             partition_label);
    snprintf(available->base_path,
             sizeof(available->base_path),
             "%s",
             base_path);
    available->read_only = read_only;
    available->active = true;
    return true;
}

static void forget_littlefs_mount(const char *partition_label)
{
    size_t i;

    for (i = 0; i < ESP32_MQUICKJS_MAX_LITTLEFS_MOUNTS; i++) {
        if (!s_littlefs_mounts[i].active ||
            strcmp(s_littlefs_mounts[i].partition_label, partition_label) != 0) {
            continue;
        }
        memset(&s_littlefs_mounts[i], 0, sizeof(s_littlefs_mounts[i]));
        return;
    }
}

static const char *fs_volume_root(JSContext *ctx,
                                  JSValue value,
                                  const char *api_name)
{
    esp32_mquickjs_fs_volume_t *volume;

    if (JS_GetClassID(ctx, value) != JS_CLASS_FS_VOLUME ||
        (volume = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "%s expects an FsVolume receiver", api_name);
        return NULL;
    }
    return volume->root;
}

static const char *startup_fs_base_path(const esp32_mquickjs_runtime_t *runtime)
{
    if (runtime != NULL && runtime->startup_fs_root[0] != '\0') {
        return runtime->startup_fs_root;
    }
    return ESP32_MQUICKJS_LITTLEFS_BASE_PATH;
}

static bool fs_root_is_available(const char *base_path)
{
    struct stat st;

    return base_path != NULL && base_path[0] == '/' &&
           strlen(base_path) < ESP32_MQUICKJS_FS_ROOT_MAX &&
           strstr(base_path, "..") == NULL &&
           stat(base_path, &st) == 0 && S_ISDIR(st.st_mode);
}

static JSValue fs_make_volume(JSContext *ctx, const char *root)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    esp32_mquickjs_fs_volume_t *volume;

    if (find_littlefs_mount_exact(root) == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowRangeError(
            ctx, "fs.volume(root) requires an exact mounted filesystem root");
    }
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_FS_VOLUME);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    volume = heap_caps_calloc(1, sizeof(*volume), MALLOC_CAP_8BIT);
    if (volume == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    snprintf(volume->root, sizeof(volume->root), "%s", root);
    JS_SetOpaque(ctx, *object, volume);
    return JS_PopGCRef(ctx, &object_ref);
}

static uint8_t *load_script_file(const char *path, size_t *out_len)
{
    FILE *file;
    long file_size;
    size_t read_len;
    uint8_t *buf;

    if (out_len == NULL) {
        return NULL;
    }
    *out_len = 0;

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    file_size = ftell(file);
    if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buf = esp32_mquickjs_memory_payload_alloc(
        "fs.read-file", (size_t)file_size + 1U,
        ESP32_MQUICKJS_MEMORY_EXTERNAL);
    if (buf == NULL) {
        fclose(file);
        return NULL;
    }

    read_len = fread(buf, 1, (size_t)file_size, file);
    fclose(file);
    if (read_len != (size_t)file_size) {
        esp32_mquickjs_memory_payload_free(buf);
        return NULL;
    }

    buf[read_len] = '\0';
    *out_len = read_len;
    return buf;
}

static JSValue fs_throw_error(JSContext *ctx,
                              const char *action,
                              const char *path,
                              int err)
{
    if (err == ENOENT) {
        return JS_ThrowReferenceError(ctx, "%s failed for %s (%s)", action, path, strerror(err));
    }
    return JS_ThrowInternalError(ctx, "%s failed for %s (%s)", action, path, strerror(err));
}

static int js_value_to_fs_path(JSContext *ctx,
                               JSValue volume_value,
                               JSValue value,
                               const char *api_name,
                               char *out_path,
                               size_t out_path_size)
{
    JSCStringBuf path_buf;
    const char *base_path = fs_volume_root(ctx, volume_value, api_name);
    const char *path;

    if (base_path == NULL) {
        return -1;
    }

    if (!JS_IsString(ctx, value)) {
        JS_ThrowTypeError(ctx, "%s expects a filesystem path string", api_name);
        return -1;
    }

    path = JS_ToCString(ctx, value, &path_buf);
    if (!esp32_mquickjs_fs_resolve_path(base_path,
                                        path,
                                        out_path,
                                        out_path_size)) {
        JS_ThrowTypeError(ctx,
                          "%s expects a path under %s",
                          api_name,
                          base_path);
        return -1;
    }

    return 0;
}


static JSValue fs_make_stat_object(JSContext *ctx, const char *path, const struct stat *st)
{
    JSGCRef entry_ref;
    JSValue *entry;

    entry = JS_PushGCRef(ctx, &entry_ref);
    *entry = JS_NewObject(ctx);
    if (JS_IsException(*entry)) {
        goto fail;
    }
    if (!esp32_mquickjs_set_property_ref(ctx, entry, "name",
                                     JS_NewString(ctx, esp32_mquickjs_fs_path_basename(path))) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "path",
                                     JS_NewString(ctx, path)) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "isDir",
                                     JS_NewBool(S_ISDIR(st->st_mode))) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "size",
                                     JS_NewInt64(ctx, (int64_t)st->st_size))) {
        goto fail;
    }

    return JS_PopGCRef(ctx, &entry_ref);

fail:
    JS_PopGCRef(ctx, &entry_ref);
    return JS_EXCEPTION;
}

bool esp32_mquickjs_mount_littlefs(bool format_if_mount_failed,
                                   bool read_only)
{
    if (s_littlefs_mounted) {
        return true;
    }
    s_littlefs_mounted = esp32_mquickjs_mount_littlefs_partition(
        ESP32_MQUICKJS_LITTLEFS_PARTITION_LABEL,
        ESP32_MQUICKJS_LITTLEFS_BASE_PATH,
        format_if_mount_failed,
        read_only);
    return s_littlefs_mounted;
}

bool esp32_mquickjs_mount_littlefs_partition(const char *partition_label,
                                             const char *base_path,
                                             bool format_if_mount_failed,
                                             bool read_only)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = base_path,
        .partition_label = partition_label,
        .format_if_mount_failed = format_if_mount_failed,
        .read_only = read_only,
        .dont_mount = false,
    };
    esp_err_t ret;
    size_t total = 0;
    size_t used = 0;

    if (partition_label == NULL || partition_label[0] == '\0' ||
        base_path == NULL || base_path[0] != '/' ||
        strlen(partition_label) >= 17U ||
        strlen(base_path) >= ESP32_MQUICKJS_FS_ROOT_MAX ||
        strstr(base_path, "..") != NULL ||
        (read_only && format_if_mount_failed)) {
        ESP_LOGE(TAG, "Invalid LittleFS partition mount parameters");
        return false;
    }

    ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format LittleFS '%s'", partition_label);
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "LittleFS partition '%s' was not found",
                     partition_label);
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS '%s' (%s)",
                     partition_label,
                     esp_err_to_name(ret));
        }
        return false;
    }

    ret = esp_littlefs_info(partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG,
                 "LittleFS '%s' mounted at %s: total=%u used=%u readOnly=%s",
                 partition_label,
                 base_path,
                 (unsigned)total,
                 (unsigned)used,
                 read_only ? "true" : "false");
    } else {
        ESP_LOGW(TAG, "LittleFS '%s' mounted but size query failed (%s)",
                 partition_label,
                 esp_err_to_name(ret));
    }
    if (!remember_littlefs_mount(partition_label, base_path, read_only)) {
        ESP_LOGW(TAG,
                 "LittleFS '%s' mounted but filesystem info registry is full",
                 partition_label);
    }
    return true;
}

void esp32_mquickjs_unmount_littlefs_partition(const char *partition_label)
{
    if (partition_label == NULL || partition_label[0] == '\0') {
        return;
    }
    esp_vfs_littlefs_unregister(partition_label);
    forget_littlefs_mount(partition_label);
}

void esp32_mquickjs_unmount_littlefs(void)
{
    if (!s_littlefs_mounted) {
        return;
    }
    esp32_mquickjs_unmount_littlefs_partition(
        ESP32_MQUICKJS_LITTLEFS_PARTITION_LABEL);
    s_littlefs_mounted = false;
}

static JSValue load_from_fs(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime,
                            const char *base_path,
                            const char *script_path)
{
    char resolved_path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    size_t source_len = 0;
    uint8_t *source;
    JSValue result;

    if (!fs_root_is_available(base_path)) {
        return JS_ThrowInternalError(ctx,
                                     "filesystem root is not available at %s",
                                     base_path);
    }
    if (!esp32_mquickjs_fs_resolve_path(base_path,
                                        script_path,
                                        resolved_path,
                                        sizeof(resolved_path))) {
        return JS_ThrowTypeError(ctx, "load(path) expects a non-empty path under %s",
                                 base_path);
    }

    source = load_script_file(resolved_path, &source_len);
    if (source == NULL) {
        return JS_ThrowReferenceError(ctx, "failed to read script: %s", resolved_path);
    }

    result = esp32_mquickjs_eval(ctx, runtime, (const char *)source, resolved_path, 0);
    esp32_mquickjs_memory_payload_free(source);
    return result;
}

JSValue esp32_mquickjs_load_from_active_fs(JSContext *ctx,
                                           esp32_mquickjs_runtime_t *runtime,
                                           const char *script_path)
{
    JSGCRef global_ref;
    JSGCRef fs_ref;
    JSValue *global_obj = JS_PushGCRef(ctx, &global_ref);
    JSValue *fs_obj = JS_PushGCRef(ctx, &fs_ref);
    const char *root;
    JSValue result;

    if (runtime != NULL && runtime->load_root_depth > 0 &&
        runtime->load_root[0] != '\0') {
        JS_PopGCRef(ctx, &fs_ref);
        JS_PopGCRef(ctx, &global_ref);
        return load_from_fs(ctx, runtime, runtime->load_root, script_path);
    }
    *global_obj = JS_GetGlobalObject(ctx);
    *fs_obj = JS_IsException(*global_obj)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global_obj, "fs");
    root = JS_IsException(*fs_obj)
        ? NULL : fs_volume_root(ctx, *fs_obj, "load(path)");
    result = root == NULL
        ? JS_EXCEPTION : load_from_fs(ctx, runtime, root, script_path);
    JS_PopGCRef(ctx, &fs_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

JSValue esp32_mquickjs_load_startup_from_active_fs(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime,
    const char *script_path)
{
    char resolved_path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    size_t source_len = 0;
    uint8_t *source;
    JSValue compiled;
    const char *startup_root = startup_fs_base_path(runtime);

    if (runtime == NULL || runtime->startup_bytecode != NULL) {
        return JS_ThrowInternalError(ctx, "startup bytecode is already loaded");
    }

    if (!esp32_mquickjs_fs_resolve_path(startup_root,
                                        script_path,
                                        resolved_path,
                                        sizeof(resolved_path))) {
        return JS_ThrowTypeError(ctx,
                                 "startup script path must stay under %s",
                                 startup_root);
    }

    source = load_script_file(resolved_path, &source_len);
    if (source == NULL) {
        return JS_ThrowReferenceError(ctx, "failed to read startup script: %s", resolved_path);
    }
    if (!JS_IsBytecode(source, source_len)) {
        JSValue result = esp32_mquickjs_eval(ctx,
                                             runtime,
                                             (const char *)source,
                                             resolved_path,
                                             0);

        esp32_mquickjs_memory_payload_free(source);
        return result;
    }

    if (JS_RelocateBytecode(ctx, source, (uint32_t)source_len) != 0) {
        esp32_mquickjs_memory_payload_free(source);
        return JS_ThrowInternalError(ctx, "failed to relocate startup bytecode");
    }
    compiled = JS_LoadBytecode(ctx, source);
    if (JS_IsException(compiled)) {
        esp32_mquickjs_memory_payload_free(source);
        return compiled;
    }

    /* MQuickJS executes directly from this buffer and its atom table. */
    runtime->startup_bytecode = source;
    ESP_LOGI(TAG,
             "Loaded precompiled startup bytecode: path=%s bytes=%u",
             resolved_path,
             (unsigned)source_len);
    return esp32_mquickjs_run(ctx, runtime, compiled);
}

JSValue esp32_mquickjs_load_from_root(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime,
                                      const char *base_path,
                                      const char *script_path)
{
    return load_from_fs(ctx,
                        runtime,
                        base_path,
                        script_path);
}

JSValue js_fs_get_root(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    const char *root;

    (void)argc;
    (void)argv;
    root = fs_volume_root(ctx, *this_val, "FsVolume.ROOT");
    return root == NULL ? JS_EXCEPTION : JS_NewString(ctx, root);
}

JSValue js_fs_volume(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSCStringBuf path_buf;
    const char *path;
    char root[ESP32_MQUICKJS_FS_ROOT_MAX];

    if (fs_volume_root(ctx, *this_val, "fs.volume(root)") == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "fs.volume(root) expects a mounted root path");
    }
    path = JS_ToCString(ctx, argv[0], &path_buf);
    if (path == NULL) {
        return JS_EXCEPTION;
    }
    if (strlen(path) >= sizeof(root)) {
        return JS_ThrowRangeError(
            ctx, "fs.volume(root) requires a shorter mounted root path");
    }
    snprintf(root, sizeof(root), "%s", path);
    return fs_make_volume(ctx, root);
}

JSValue js_fs_volume_constructor(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "FsVolume cannot be constructed directly; use fs.volume(root)");
}

void js_fs_volume_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    heap_caps_free(opaque);
}

JSValue js_fs_info(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    const char *root = fs_volume_root(ctx, *this_val, "fs.info()");
    esp32_mquickjs_littlefs_mount_t *mount;
    size_t total = 0;
    size_t used = 0;
    JSGCRef result_ref;
    JSValue *result;
    esp_err_t ret;

    (void)argv;
    if (root == NULL) {
        return JS_EXCEPTION;
    }
    if (argc != 0) {
        return JS_ThrowTypeError(ctx, "fs.info() expects no arguments");
    }
    mount = find_littlefs_mount_by_root(root);
    if (mount == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "filesystem information is unavailable for %s",
                                     root);
    }
    ret = esp_littlefs_info(mount->partition_label, &total, &used);
    if (ret != ESP_OK) {
        return JS_ThrowInternalError(ctx,
                                     "filesystem information query failed for %s (%s)",
                                     root,
                                     esp_err_to_name(ret));
    }

    result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         result,
                                         "root",
                                         JS_NewString(ctx, root)) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         result,
                                         "readOnly",
                                         JS_NewBool(mount->read_only)) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         result,
                                         "totalBytes",
                                         JS_NewInt64(ctx, (int64_t)total)) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         result,
                                         "usedBytes",
                                         JS_NewInt64(ctx, (int64_t)used)) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         result,
                                         "freeBytes",
                                         JS_NewInt64(ctx,
                                                     (int64_t)(total >= used
                                                                   ? total - used
                                                                   : 0U)))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static bool fs_parse_watch_options(JSContext *ctx,
                                   int argc,
                                   JSValue *argv,
                                   uint32_t *capacity)
{
    JSGCRef value_ref;
    JSValue *value;
    static const char *const allowed[] = {"capacity"};
    uint32_t parsed_capacity;

    if (capacity == NULL) {
        return false;
    }
    *capacity = ESP32_MQUICKJS_FS_CHANGE_QUEUE_LEN;
    if (argc == 0 || JS_IsUndefined(argv[0])) {
        return true;
    }
    if (argc != 1 ||
        !esp32_mquickjs_validate_plain_options(
            ctx, argv[0], "fs.watch(options?)", allowed, 1)) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx, "fs.watch(options?) expects { capacity? }");
        }
        return false;
    }

    value = JS_PushGCRef(ctx, &value_ref);
    *value = JS_GetPropertyStr(ctx, argv[0], "capacity");
    if (!JS_IsUndefined(*value) &&
        !esp32_mquickjs_value_to_bounded_u32(
            ctx, *value, 1U, ESP32_MQUICKJS_FS_CHANGE_QUEUE_MAX_LEN,
            &parsed_capacity)) {
        JS_ThrowRangeError(
            ctx, "fs.watch({ capacity }) expects an integer in 1..%u",
            (unsigned)ESP32_MQUICKJS_FS_CHANGE_QUEUE_MAX_LEN);
        JS_PopGCRef(ctx, &value_ref);
        return false;
    }
    if (!JS_IsUndefined(*value)) {
        *capacity = parsed_capacity;
    }
    JS_PopGCRef(ctx, &value_ref);
    return true;
}

JSValue js_fs_watch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    esp32_mquickjs_fs_runtime_t *state = fs_runtime(runtime);
    const char *root = fs_volume_root(ctx, *this_val, "fs.watch()");
    JSGCRef queue_ref;
    JSValue *queue_object;
    esp32_mquickjs_event_queue_t *changes;
    esp32_mquickjs_fs_change_source_t *source;
    uint32_t capacity;

    if (root == NULL) {
        return JS_EXCEPTION;
    }
    if (!fs_parse_watch_options(ctx, argc, argv, &capacity)) {
        return JS_EXCEPTION;
    }
    if (state == NULL || state->lock == NULL) {
        return JS_ThrowInternalError(ctx, "fs.watch() requires an active filesystem runtime");
    }
    source = heap_caps_calloc(1, sizeof(*source), MALLOC_CAP_8BIT);
    if (source == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    source->owner = state;
    snprintf(source->root, sizeof(source->root), "%s", root);

    queue_object = JS_PushGCRef(ctx, &queue_ref);
    *queue_object = esp32_mquickjs_event_queue_new(
        ctx,
        runtime,
        sizeof(esp32_mquickjs_fs_change_event_t),
        capacity,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST,
        fs_change_to_js,
        NULL,
        fs_change_queue_closed,
        source);
    if (JS_IsException(*queue_object)) {
        heap_caps_free(source);
        JS_PopGCRef(ctx, &queue_ref);
        return JS_EXCEPTION;
    }
    changes = esp32_mquickjs_event_queue_from_value(ctx, *queue_object);
    if (changes == NULL) {
        JS_PopGCRef(ctx, &queue_ref);
        return JS_ThrowInternalError(ctx, "fs.watch() could not create its change queue");
    }

    xSemaphoreTake(state->lock, portMAX_DELAY);
    source->changes = changes;
    source->next = state->sources;
    state->sources = source;
    xSemaphoreGive(state->lock);
    return JS_PopGCRef(ctx, &queue_ref);
}

JSValue js_framework_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    JSCStringBuf path_buf;
    const char *path;
    JSValue result;

    (void)this_val;
    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "framework.load(path) expects a script path");
    }
    if (runtime == NULL) {
        return JS_ThrowInternalError(ctx, "JavaScript runtime is not active");
    }
    if (!s_littlefs_mounted || runtime->load_root_depth == UINT16_MAX) {
        return JS_ThrowInternalError(ctx, "framework filesystem is not available");
    }
    path = JS_ToCString(ctx, argv[0], &path_buf);
    snprintf(runtime->load_root,
             sizeof(runtime->load_root),
             "%s",
             ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
    runtime->load_root_depth++;
    result = esp32_mquickjs_load_from_root(ctx,
                                           runtime,
                                           ESP32_MQUICKJS_LITTLEFS_BASE_PATH "/_sys",
                                           path);
    runtime->load_root_depth--;
    if (runtime->load_root_depth == 0) {
        runtime->load_root[0] = '\0';
    }
    return result;
}

#define ESP32_MQUICKJS_FS_FUTURE_MAX_ENTRIES 64U

typedef enum {
    FS_FUTURE_OPEN,
    FS_FUTURE_LIST,
    FS_FUTURE_STAT,
    FS_FUTURE_EXISTS,
    FS_FUTURE_READ_TEXT,
    FS_FUTURE_WRITE_TEXT,
    FS_FUTURE_APPEND_TEXT,
    FS_FUTURE_REMOVE,
    FS_FUTURE_RENAME,
    FS_FUTURE_MKDIR,
} fs_future_kind_t;

typedef struct {
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    struct stat stat_value;
} fs_future_entry_t;

struct esp32_mquickjs_future_driver_state {
    fs_future_kind_t kind;
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    char to_path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    char mode[8];
    FILE *file;
    char *data;
    size_t data_length;
    size_t max_bytes;
    size_t actual_bytes;
    struct stat stat_value;
    fs_future_entry_t *entries;
    size_t entry_count;
    int error_number;
    bool result;
    bool opened_mutation;
    esp32_mquickjs_resource_key_t resource_key;
    _Atomic bool completed;
    bool cancelled;
};

static JSValue fs_throw_read_limit_error(
    JSContext *ctx,
    const esp32_mquickjs_future_driver_state_t *state)
{
    JSGCRef error_ref;
    JSValue *error;

    (void)JS_ThrowRangeError(
        ctx, "readText() refused %s because it exceeds maxBytes (%u)",
        state->path, (unsigned)state->max_bytes);
    if (!JS_HasException(ctx)) {
        return JS_EXCEPTION;
    }
    error = JS_PushGCRef(ctx, &error_ref);
    *error = JS_GetException(ctx);
    if (JS_GetClassID(ctx, *error) >= 0 &&
        (JS_IsException(JS_SetPropertyStr(
             ctx, *error, "code",
             JS_NewString(ctx, "FS_READ_LIMIT_EXCEEDED"))) ||
         JS_IsException(JS_SetPropertyStr(
             ctx, *error, "path", JS_NewString(ctx, state->path))) ||
         JS_IsException(JS_SetPropertyStr(
             ctx, *error, "maxBytes",
             JS_NewUint32(ctx, (uint32_t)state->max_bytes))) ||
         JS_IsException(JS_SetPropertyStr(
             ctx, *error, "actualBytes",
             JS_NewUint32(ctx, (uint32_t)state->actual_bytes))))) {
        JS_PopGCRef(ctx, &error_ref);
        return JS_EXCEPTION;
    }
    return JS_Throw(ctx, JS_PopGCRef(ctx, &error_ref));
}

static bool fs_number_to_bounded_size(JSContext *ctx,
                                      JSValue value,
                                      size_t minimum,
                                      size_t maximum,
                                      size_t *result)
{
    double number;
    size_t converted;

    if (result == NULL || !JS_IsNumber(ctx, value) ||
        JS_ToNumber(ctx, &number, value) != 0 || !isfinite(number) ||
        number < (double)minimum || number > (double)maximum) {
        return false;
    }
    converted = (size_t)number;
    if ((double)converted != number) {
        return false;
    }
    *result = converted;
    return true;
}

static bool fs_parse_read_text_options(JSContext *ctx,
                                       int argc,
                                       JSGCRef *argv,
                                       size_t *max_bytes)
{
    JSGCRef value_ref;
    JSValue *value;
    static const char *const allowed[] = {"maxBytes"};

    if (max_bytes == NULL) {
        return false;
    }
    *max_bytes = CONFIG_ESP32_MQUICKJS_FS_READ_TEXT_MAX_BYTES;
    if (argc < 2 || JS_IsUndefined(argv[1].val) || JS_IsNull(argv[1].val)) {
        return true;
    }
    if (!esp32_mquickjs_validate_plain_options(
            ctx, argv[1].val, "fs.readText(path, options?)", allowed, 1)) {
        return false;
    }

    value = JS_PushGCRef(ctx, &value_ref);
    *value = JS_GetPropertyStr(ctx, argv[1].val, "maxBytes");
    if (!JS_IsUndefined(*value) &&
        !fs_number_to_bounded_size(
            ctx, *value, 1U,
            CONFIG_ESP32_MQUICKJS_FS_READ_TEXT_MAX_BYTES, max_bytes)) {
        JS_ThrowRangeError(
            ctx,
            "fs.readText(path, { maxBytes }) expects an integer in 1..%u",
            (unsigned)CONFIG_ESP32_MQUICKJS_FS_READ_TEXT_MAX_BYTES);
        goto fail;
    }
    JS_PopGCRef(ctx, &value_ref);
    return true;

fail:
    JS_PopGCRef(ctx, &value_ref);
    return false;
}

static bool fs_stream_mode_valid(const char *mode)
{
    return mode != NULL &&
           (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0 ||
            strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0 ||
            strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0 ||
            strcmp(mode, "r+") == 0 || strcmp(mode, "rb+") == 0 ||
            strcmp(mode, "r+b") == 0 || strcmp(mode, "w+") == 0 ||
            strcmp(mode, "wb+") == 0 || strcmp(mode, "w+b") == 0 ||
            strcmp(mode, "a+") == 0 || strcmp(mode, "ab+") == 0 ||
            strcmp(mode, "a+b") == 0);
}

static void fs_future_release(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    esp32_mquickjs_memory_payload_free(state->data);
    esp32_mquickjs_memory_payload_free(state->entries);
    if (state->file != NULL) {
        fclose(state->file);
    }
    heap_caps_free(state);
}

static bool fs_future_prepare_common(
    JSContext *ctx,
    JSGCRef *receiver_ref,
    fs_future_kind_t kind,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    const char *api_name;

    if (out_state == NULL) {
        return false;
    }
    api_name = kind == FS_FUTURE_OPEN ? "fs.open(path, mode?)" :
               kind == FS_FUTURE_LIST ? "fs.list(path?)" :
               kind == FS_FUTURE_STAT ? "fs.stat(path)" :
               kind == FS_FUTURE_EXISTS ? "fs.exists(path)" :
               kind == FS_FUTURE_READ_TEXT ? "fs.readText(path, options?)" :
               kind == FS_FUTURE_WRITE_TEXT ? "fs.writeText(path, text)" :
               kind == FS_FUTURE_APPEND_TEXT ? "fs.appendText(path, text)" :
               kind == FS_FUTURE_REMOVE ? "fs.remove(path)" :
               kind == FS_FUTURE_RENAME ? "fs.rename(fromPath, toPath)" :
               "fs.mkdir(path)";
    if ((kind == FS_FUTURE_OPEN && (argc < 1 || argc > 2)) ||
        (kind == FS_FUTURE_LIST && argc > 1) ||
        (kind == FS_FUTURE_READ_TEXT && (argc < 1 || argc > 2)) ||
        (kind != FS_FUTURE_OPEN && kind != FS_FUTURE_LIST &&
         kind != FS_FUTURE_RENAME && kind != FS_FUTURE_READ_TEXT &&
         kind != FS_FUTURE_WRITE_TEXT && kind != FS_FUTURE_APPEND_TEXT &&
         argc != 1) ||
        (kind == FS_FUTURE_RENAME && argc != 2) ||
        ((kind == FS_FUTURE_WRITE_TEXT || kind == FS_FUTURE_APPEND_TEXT) && argc != 2)) {
        JS_ThrowTypeError(ctx, "%s received invalid arguments", api_name);
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    atomic_init(&state->completed, false);
    state->kind = kind;
    if (kind == FS_FUTURE_OPEN) {
        JSCStringBuf mode_buf;
        const char *mode = "r";

        if (argc == 2 && !JS_IsUndefined(argv[1].val) &&
            !JS_IsNull(argv[1].val)) {
            if (!JS_IsString(ctx, argv[1].val)) {
                fs_future_release(state);
                JS_ThrowTypeError(ctx, "%s expects mode to be a string", api_name);
                return false;
            }
            mode = JS_ToCString(ctx, argv[1].val, &mode_buf);
            if (mode == NULL) {
                fs_future_release(state);
                return false;
            }
        }
        if (strlen(mode) >= sizeof(state->mode)) {
            fs_future_release(state);
            JS_ThrowTypeError(ctx, "unsupported stream mode: %s", mode);
            return false;
        }
        if (!fs_stream_mode_valid(mode)) {
            fs_future_release(state);
            JS_ThrowTypeError(ctx, "unsupported stream mode: %s", mode);
            return false;
        }
        snprintf(state->mode, sizeof(state->mode), "%s", mode);
    }
    if (kind == FS_FUTURE_LIST && argc == 0) {
        const char *root = fs_volume_root(ctx, receiver_ref->val, api_name);

        if (root == NULL) {
            fs_future_release(state);
            return false;
        }
        snprintf(state->path, sizeof(state->path), "%s", root);
    } else if (js_value_to_fs_path(ctx, receiver_ref->val, argv[0].val, api_name,
                                   state->path, sizeof(state->path)) != 0) {
        fs_future_release(state);
        return false;
    }
    if (kind == FS_FUTURE_RENAME &&
        js_value_to_fs_path(ctx, receiver_ref->val, argv[1].val, api_name,
                            state->to_path, sizeof(state->to_path)) != 0) {
        fs_future_release(state);
        return false;
    }
    state->resource_key = esp32_mquickjs_fs_resource_key_for_path(state->path);
    if (state->resource_key == NULL) {
        fs_future_release(state);
        JS_ThrowInternalError(ctx, "%s could not resolve its filesystem mount",
                              api_name);
        return false;
    }
    if (kind == FS_FUTURE_READ_TEXT &&
        !fs_parse_read_text_options(ctx, argc, argv, &state->max_bytes)) {
        fs_future_release(state);
        return false;
    }
    if (kind == FS_FUTURE_WRITE_TEXT || kind == FS_FUTURE_APPEND_TEXT) {
        JSCStringBuf text_buf;
        const char *text;

        if (!JS_IsString(ctx, argv[1].val)) {
            fs_future_release(state);
            JS_ThrowTypeError(ctx, "%s expects a text string", api_name);
            return false;
        }
        text = JS_ToCStringLen(ctx, &state->data_length, argv[1].val, &text_buf);
        if (text == NULL) {
            fs_future_release(state);
            return false;
        }
        state->data = esp32_mquickjs_memory_payload_alloc(
            "fs.write-file", state->data_length + 1U,
            ESP32_MQUICKJS_MEMORY_EXTERNAL);
        if (state->data == NULL) {
            fs_future_release(state);
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
        memcpy(state->data, text, state->data_length);
        state->data[state->data_length] = '\0';
    }
    *out_state = state;
    return true;
}

#define FS_PREPARE(name, kind_value) \
    static bool name(JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv, \
                     esp32_mquickjs_future_driver_state_t **out_state) \
    { \
        return fs_future_prepare_common(ctx, this_ref, kind_value, argc, \
                                        argv, out_state); \
    }

FS_PREPARE(fs_list_future_prepare, FS_FUTURE_LIST)
FS_PREPARE(fs_open_future_prepare, FS_FUTURE_OPEN)
FS_PREPARE(fs_stat_future_prepare, FS_FUTURE_STAT)
FS_PREPARE(fs_exists_future_prepare, FS_FUTURE_EXISTS)
FS_PREPARE(fs_read_text_future_prepare, FS_FUTURE_READ_TEXT)
FS_PREPARE(fs_write_text_future_prepare, FS_FUTURE_WRITE_TEXT)
FS_PREPARE(fs_append_text_future_prepare, FS_FUTURE_APPEND_TEXT)
FS_PREPARE(fs_remove_future_prepare, FS_FUTURE_REMOVE)
FS_PREPARE(fs_rename_future_prepare, FS_FUTURE_RENAME)
FS_PREPARE(fs_mkdir_future_prepare, FS_FUTURE_MKDIR)

#undef FS_PREPARE

static int fs_read_text_bounded(
    esp32_mquickjs_future_driver_state_t *state)
{
    FILE *file;
    struct stat stat_value;
    size_t capacity;
    size_t length = 0;
    char *data;
    int result = 0;

    file = fopen(state->path, "rb");
    if (file == NULL) {
        return errno != 0 ? errno : EIO;
    }
    if (fstat(fileno(file), &stat_value) != 0) {
        result = errno != 0 ? errno : EIO;
        goto done;
    }
    if (stat_value.st_size < 0) {
        result = EIO;
        goto done;
    }
    state->actual_bytes = (size_t)stat_value.st_size;
    if ((uint64_t)stat_value.st_size > (uint64_t)state->max_bytes) {
        result = EFBIG;
        goto done;
    }

    capacity = (size_t)stat_value.st_size;
    if (capacity == 0) {
        capacity = 1U;
    }
    data = esp32_mquickjs_memory_payload_alloc(
        "fs.read-text", capacity + 1U,
        ESP32_MQUICKJS_MEMORY_EXTERNAL);
    if (data == NULL) {
        result = ENOMEM;
        goto done;
    }

    for (;;) {
        size_t available = capacity - length;
        size_t read_length;

        if (available == 0) {
            if (capacity >= state->max_bytes) {
                int extra = fgetc(file);

                if (extra != EOF) {
                    state->actual_bytes = state->max_bytes + 1U;
                    result = EFBIG;
                } else if (ferror(file)) {
                    result = errno != 0 ? errno : EIO;
                }
                break;
            }
            {
                size_t next_capacity = capacity > state->max_bytes / 2U
                                           ? state->max_bytes
                                           : capacity * 2U;
                char *grown = esp32_mquickjs_memory_payload_realloc(
                    "fs.read-text", data, next_capacity + 1U,
                    ESP32_MQUICKJS_MEMORY_EXTERNAL);

                if (grown == NULL) {
                    result = ENOMEM;
                    break;
                }
                data = grown;
                capacity = next_capacity;
                continue;
            }
        }

        read_length = fread(data + length, 1, available, file);
        length += read_length;
        if (read_length < available) {
            if (ferror(file)) {
                result = errno != 0 ? errno : EIO;
            }
            break;
        }
    }

    if (result == 0) {
        data[length] = '\0';
        state->data = data;
        state->data_length = length;
        state->actual_bytes = length;
        data = NULL;
    }
    esp32_mquickjs_memory_payload_free(data);

done:
    if (fclose(file) != 0 && result == 0) {
        result = errno != 0 ? errno : EIO;
    }
    return result;
}

static uint32_t fs_atomic_path_hash(const char *path)
{
    const uint8_t *cursor = (const uint8_t *)path;
    uint32_t hash = 2166136261U;

    while (cursor != NULL && *cursor != '\0') {
        hash ^= *cursor++;
        hash *= 16777619U;
    }
    return hash;
}

static int fs_open_atomic_temp(const char *path,
                               char *temp_path,
                               size_t temp_path_size,
                               FILE **out_file)
{
    const char *separator = strrchr(path, '/');
    size_t directory_length;
    uint32_t hash;
    uint32_t attempt;

    if (separator == NULL || separator == path || temp_path == NULL ||
        out_file == NULL) {
        return EINVAL;
    }
    directory_length = (size_t)(separator - path);
    hash = fs_atomic_path_hash(path);
    *out_file = NULL;
    for (attempt = 0; attempt < ESP32_MQUICKJS_FS_ATOMIC_TEMP_ATTEMPTS;
         ++attempt) {
        uint32_t counter = atomic_fetch_add_explicit(
            &s_fs_atomic_temp_counter, 1U, memory_order_relaxed);
        int needed = snprintf(temp_path, temp_path_size,
                              "%.*s/.qjs-%08" PRIx32 "-%08" PRIx32 ".tmp",
                              (int)directory_length, path, hash, counter);
        int fd;

        if (needed <= 0 || (size_t)needed >= temp_path_size) {
            return ENAMETOOLONG;
        }
        fd = open(temp_path, O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0600);
        if (fd >= 0) {
            *out_file = fdopen(fd, "wb");
            if (*out_file == NULL) {
                int open_error = errno != 0 ? errno : EIO;

                close(fd);
                unlink(temp_path);
                return open_error;
            }
            return 0;
        }
        if (errno != EEXIST) {
            return errno != 0 ? errno : EIO;
        }
    }
    return EEXIST;
}

static int fs_write_file_and_sync(FILE *file,
                                  const char *data,
                                  size_t data_length)
{
    size_t written = 0;
    int result = 0;

    while (written < data_length) {
        size_t chunk = fwrite(data + written, 1, data_length - written, file);

        if (chunk == 0) {
            result = errno != 0 ? errno : EIO;
            break;
        }
        written += chunk;
    }
    if (result == 0 && fflush(file) != 0) {
        result = errno != 0 ? errno : EIO;
    }
    if (result == 0 && fsync(fileno(file)) != 0) {
        result = errno != 0 ? errno : EIO;
    }
    if (fclose(file) != 0 && result == 0) {
        result = errno != 0 ? errno : EIO;
    }
    return result;
}

static int fs_atomic_open_adapter(const char *target_path,
                                  char *temp_path,
                                  size_t temp_path_size,
                                  void **out_handle,
                                  void *opaque)
{
    FILE *file = NULL;
    int result;

    (void)opaque;
    result = fs_open_atomic_temp(
        target_path, temp_path, temp_path_size, &file);
    if (out_handle != NULL) {
        *out_handle = file;
    }
    return result;
}

static int fs_atomic_write_sync_close_adapter(void *handle,
                                              const char *data,
                                              size_t data_length,
                                              void *opaque)
{
    (void)opaque;
    return fs_write_file_and_sync(handle, data, data_length);
}

static int fs_atomic_replace_adapter(const char *temp_path,
                                     const char *target_path,
                                     void *opaque)
{
    (void)opaque;
    return rename(temp_path, target_path) == 0
               ? 0
               : (errno != 0 ? errno : EIO);
}

static void fs_atomic_remove_adapter(const char *temp_path, void *opaque)
{
    (void)opaque;
    (void)unlink(temp_path);
}

static int fs_write_text_atomic(
    const esp32_mquickjs_future_driver_state_t *state)
{
    static const esp32_mquickjs_fs_atomic_write_ops_t ops = {
        .open_temp = fs_atomic_open_adapter,
        .write_sync_close = fs_atomic_write_sync_close_adapter,
        .replace = fs_atomic_replace_adapter,
        .remove_temp = fs_atomic_remove_adapter,
    };
    char temp_path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];

    return esp32_mquickjs_fs_atomic_write(
        state->path, state->data, state->data_length,
        temp_path, sizeof(temp_path), &ops, NULL);
}

static void fs_future_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;

    if (state == NULL) {
        return;
    }
    errno = 0;
    if (state->kind == FS_FUTURE_OPEN) {
        bool path_existed = access(state->path, F_OK) == 0;

        state->file = fopen(state->path, state->mode);
        if (state->file == NULL) {
            state->error_number = errno;
        } else {
            state->opened_mutation =
                state->mode[0] == 'w' ||
                (state->mode[0] == 'a' && !path_existed);
        }
    } else if (state->kind == FS_FUTURE_LIST) {
        DIR *dir = opendir(state->path);

        if (dir == NULL) {
            state->error_number = errno;
        } else {
            struct dirent *entry_raw;

            while ((entry_raw = readdir(dir)) != NULL) {
                fs_future_entry_t *entry;
                fs_future_entry_t *grown;
                int needed;

                if (strcmp(entry_raw->d_name, ".") == 0 ||
                    strcmp(entry_raw->d_name, "..") == 0) {
                    continue;
                }
                if (state->entry_count >= ESP32_MQUICKJS_FS_FUTURE_MAX_ENTRIES) {
                    state->error_number = E2BIG;
                    break;
                }
                grown = esp32_mquickjs_memory_payload_realloc(
                    "fs.read-dir", state->entries,
                    (state->entry_count + 1U) * sizeof(*state->entries),
                    ESP32_MQUICKJS_MEMORY_EXTERNAL);
                if (grown == NULL) {
                    state->error_number = ENOMEM;
                    break;
                }
                state->entries = grown;
                entry = &state->entries[state->entry_count];
                needed = snprintf(entry->path, sizeof(entry->path),
                                  "%s/%s", state->path, entry_raw->d_name);
                if (needed <= 0 || (size_t)needed >= sizeof(entry->path)) {
                    state->error_number = ENAMETOOLONG;
                    break;
                }
                if (stat(entry->path, &entry->stat_value) != 0) {
                    state->error_number = errno;
                    break;
                }
                state->entry_count++;
            }
            closedir(dir);
        }
    } else if (state->kind == FS_FUTURE_STAT || state->kind == FS_FUTURE_EXISTS) {
        state->result = stat(state->path, &state->stat_value) == 0;
        if (!state->result && state->kind == FS_FUTURE_STAT) {
            state->error_number = errno;
        }
    } else if (state->kind == FS_FUTURE_READ_TEXT) {
        state->error_number = fs_read_text_bounded(state);
    } else if (state->kind == FS_FUTURE_WRITE_TEXT) {
        state->error_number = fs_write_text_atomic(state);
        if (state->error_number == 0) {
            esp32_mquickjs_fs_notify_change(
                ESP32_MQUICKJS_FS_CHANGE_WRITE, state->path, NULL);
        }
    } else if (state->kind == FS_FUTURE_APPEND_TEXT) {
        bool path_existed = access(state->path, F_OK) == 0;
        FILE *file = fopen(state->path, "ab");

        if (file == NULL) {
            state->error_number = errno;
        } else {
            state->error_number = fs_write_file_and_sync(
                file, state->data, state->data_length);
            if (state->error_number == 0 &&
                (!path_existed || state->data_length > 0U)) {
                esp32_mquickjs_fs_notify_change(
                    ESP32_MQUICKJS_FS_CHANGE_WRITE, state->path, NULL);
            }
        }
    } else if (state->kind == FS_FUTURE_REMOVE) {
        if (stat(state->path, &state->stat_value) != 0 ||
            (S_ISDIR(state->stat_value.st_mode)
                 ? rmdir(state->path) : remove(state->path)) != 0) {
            state->error_number = errno;
        } else {
            state->result = true;
        }
    } else if (state->kind == FS_FUTURE_RENAME) {
        if (rename(state->path, state->to_path) != 0) {
            state->error_number = errno;
        } else {
            state->result = true;
        }
    } else if (mkdir(state->path, 0777) != 0) {
        state->error_number = errno;
    } else {
        state->result = true;
    }
    if (state->error_number == 0 && state->kind == FS_FUTURE_REMOVE) {
        esp32_mquickjs_fs_notify_change(
            ESP32_MQUICKJS_FS_CHANGE_REMOVE, state->path, NULL);
    } else if (state->error_number == 0 && state->kind == FS_FUTURE_RENAME) {
        esp32_mquickjs_fs_notify_change(
            ESP32_MQUICKJS_FS_CHANGE_RENAME, state->path, state->to_path);
    } else if (state->error_number == 0 && state->kind == FS_FUTURE_MKDIR) {
        esp32_mquickjs_fs_notify_change(
            ESP32_MQUICKJS_FS_CHANGE_MKDIR, state->path, NULL);
    }
    atomic_store_explicit(&state->completed, true, memory_order_release);
}

static bool fs_future_start(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime,
                            esp32_mquickjs_future_token_t token,
                            esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return false;
    }
    if (!esp32_mquickjs_future_submit_worker(runtime, token,
                                             fs_future_worker, state)) {
        JS_ThrowInternalError(ctx, "filesystem Future worker queue is busy");
        return false;
    }
    return true;
}

static esp32_mquickjs_future_poll_t fs_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL && atomic_load_explicit(
                                &state->completed, memory_order_acquire)
        ? ESP32_MQUICKJS_FUTURE_READY
        : ESP32_MQUICKJS_FUTURE_PENDING;
}

static const char *fs_future_action(const esp32_mquickjs_future_driver_state_t *state)
{
    return state->kind == FS_FUTURE_OPEN ? "open()" :
           state->kind == FS_FUTURE_LIST ? "list()" :
           state->kind == FS_FUTURE_STAT ? "stat()" :
           state->kind == FS_FUTURE_READ_TEXT ? "readText()" :
           state->kind == FS_FUTURE_WRITE_TEXT ? "writeText()" :
           state->kind == FS_FUTURE_APPEND_TEXT ? "appendText()" :
           state->kind == FS_FUTURE_REMOVE ? "remove()" :
           state->kind == FS_FUTURE_RENAME ? "rename()" : "mkdir()";
}

static JSValue fs_future_finish(JSContext *ctx,
                                esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->cancelled) {
        return JS_ThrowInternalError(ctx, "filesystem operation cancelled");
    }
    if (state->error_number != 0) {
        if (state->kind == FS_FUTURE_READ_TEXT &&
            state->error_number == EFBIG) {
            return fs_throw_read_limit_error(ctx, state);
        }
        return fs_throw_error(ctx, fs_future_action(state), state->path,
                              state->error_number);
    }
    if (state->kind == FS_FUTURE_OPEN) {
        JSGCRef global_ref;
        JSValue *global_obj = JS_PushGCRef(ctx, &global_ref);
        JSValue result;

        *global_obj = JS_GetGlobalObject(ctx);
        result = JS_IsException(*global_obj)
            ? JS_EXCEPTION
            : esp32_mquickjs_stream_adopt_file(ctx, *global_obj, state->path,
                                               state->mode, state->file,
                                               state->opened_mutation);
        if (!JS_IsException(result)) {
            state->file = NULL;
        } else {
            /* adopt_file consumes the FILE handle on every path. */
            state->file = NULL;
        }
        JS_PopGCRef(ctx, &global_ref);
        return result;
    }
    if (state->kind == FS_FUTURE_LIST) {
        JSGCRef entries_ref;
        JSValue *entries = JS_PushGCRef(ctx, &entries_ref);
        uint32_t index;

        *entries = JS_NewArray(ctx, state->entry_count);
        for (index = 0; !JS_IsException(*entries) && index < state->entry_count; ++index) {
            JSValue entry = fs_make_stat_object(ctx,
                                                state->entries[index].path,
                                                &state->entries[index].stat_value);
            if (JS_IsException(entry) ||
                JS_IsException(JS_SetPropertyUint32(ctx, *entries, index, entry))) {
                JS_PopGCRef(ctx, &entries_ref);
                return JS_EXCEPTION;
            }
        }
        return JS_PopGCRef(ctx, &entries_ref);
    }
    if (state->kind == FS_FUTURE_STAT) {
        return fs_make_stat_object(ctx, state->path, &state->stat_value);
    }
    if (state->kind == FS_FUTURE_EXISTS) {
        return JS_NewBool(state->result);
    }
    if (state->kind == FS_FUTURE_READ_TEXT) {
        return JS_NewStringLen(ctx, state->data, state->data_length);
    }
    if (state->kind == FS_FUTURE_WRITE_TEXT || state->kind == FS_FUTURE_APPEND_TEXT) {
        return JS_NewInt64(ctx, (int64_t)state->data_length);
    }
    return JS_NewBool(state->result);
}

static esp32_mquickjs_cancel_result_t fs_future_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL ||
        atomic_load_explicit(&state->completed, memory_order_acquire) ||
        state->cancelled) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    state->cancelled = true;
    return ESP32_MQUICKJS_CANCEL_REQUESTED;
}

static void fs_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    fs_future_release(state);
}

static esp32_mquickjs_resource_key_t fs_future_resource_key(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL ? state->resource_key : NULL;
}

#define FS_FUTURE_DRIVER(name, prepare_fn) \
    static const esp32_mquickjs_future_driver_t name = { \
        .capture = prepare_fn, \
        .start = fs_future_start, \
        .poll = fs_future_poll, \
        .finish = fs_future_finish, \
        .cancel = fs_future_cancel, \
        .destroy = fs_future_destroy, \
        .resource_key = fs_future_resource_key, \
    }

FS_FUTURE_DRIVER(s_fs_list_driver, fs_list_future_prepare);
FS_FUTURE_DRIVER(s_fs_open_driver, fs_open_future_prepare);
FS_FUTURE_DRIVER(s_fs_stat_driver, fs_stat_future_prepare);
FS_FUTURE_DRIVER(s_fs_exists_driver, fs_exists_future_prepare);
FS_FUTURE_DRIVER(s_fs_read_text_driver, fs_read_text_future_prepare);
FS_FUTURE_DRIVER(s_fs_write_text_driver, fs_write_text_future_prepare);
FS_FUTURE_DRIVER(s_fs_append_text_driver, fs_append_text_future_prepare);
FS_FUTURE_DRIVER(s_fs_remove_driver, fs_remove_future_prepare);
FS_FUTURE_DRIVER(s_fs_rename_driver, fs_rename_future_prepare);
FS_FUTURE_DRIVER(s_fs_mkdir_driver, fs_mkdir_future_prepare);

#undef FS_FUTURE_DRIVER

bool esp32_mquickjs_init_fs_runtime(JSContext *ctx,
                                    esp32_mquickjs_runtime_t *runtime)
{
    static const char *names[] = {
        "open", "list", "stat", "exists", "readText", "writeText",
        "appendText", "remove", "rename", "mkdir",
    };
    static const esp32_mquickjs_future_driver_t *drivers[] = {
        &s_fs_open_driver, &s_fs_list_driver, &s_fs_stat_driver, &s_fs_exists_driver,
        &s_fs_read_text_driver, &s_fs_write_text_driver,
        &s_fs_append_text_driver, &s_fs_remove_driver,
        &s_fs_rename_driver, &s_fs_mkdir_driver,
    };
    JSGCRef global_ref;
    JSGCRef fs_ref;
    JSValue *global_obj;
    JSValue *fs_obj;
    size_t index;
    bool result = true;

    if (runtime == NULL || runtime->fs_state != NULL) {
        return false;
    }
    runtime->fs_state = heap_caps_calloc(
        1, sizeof(esp32_mquickjs_fs_runtime_t), MALLOC_CAP_8BIT);
    if (runtime->fs_state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    fs_runtime(runtime)->lock = xSemaphoreCreateMutex();
    if (fs_runtime(runtime)->lock == NULL) {
        esp32_mquickjs_deinit_fs_runtime(runtime);
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    global_obj = JS_PushGCRef(ctx, &global_ref);
    fs_obj = JS_PushGCRef(ctx, &fs_ref);
    *global_obj = JS_GetGlobalObject(ctx);
    *fs_obj = JS_IsException(*global_obj)
        ? JS_EXCEPTION : fs_make_volume(ctx, ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
    if (!JS_IsException(*fs_obj)) {
        result = esp32_mquickjs_set_property_ref(ctx, global_obj, "fs", *fs_obj);
    } else {
        result = false;
    }
    for (index = 0; result && index < sizeof(names) / sizeof(names[0]); ++index) {
        JSGCRef method_ref;
        JSValue *method = JS_PushGCRef(ctx, &method_ref);

        *method = JS_IsException(*fs_obj)
            ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *fs_obj, names[index]);
        result = !JS_IsException(*method) &&
                 esp32_mquickjs_future_register_driver(ctx, runtime,
                                                       *method, drivers[index]);
        JS_PopGCRef(ctx, &method_ref);
    }
    if (!result && !JS_IsException(*fs_obj)) {
        JS_ThrowInternalError(ctx, "failed to register filesystem Future drivers");
    }
    JS_PopGCRef(ctx, &fs_ref);
    JS_PopGCRef(ctx, &global_ref);
    if (!result) {
        esp32_mquickjs_deinit_fs_runtime(runtime);
    }
    return result;
}

void esp32_mquickjs_deinit_fs_runtime(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_fs_runtime_t *state = fs_runtime(runtime);

    if (state == NULL) {
        return;
    }
    if (state->lock != NULL) {
        vSemaphoreDelete(state->lock);
    }
    heap_caps_free(state);
    runtime->fs_state = NULL;
}

static JSValue fs_future_call_and_wait(JSContext *ctx,
                                       JSValue receiver,
                                       const char *method_name,
                                       int argc,
                                       JSValue *argv)
{
    JSGCRef receiver_ref;
    JSGCRef method_ref;
    JSValue *rooted_receiver = JS_PushGCRef(ctx, &receiver_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *rooted_receiver = receiver;
    *method = JS_GetPropertyStr(ctx, *rooted_receiver, method_name);
    result = JS_IsException(*method)
        ? JS_EXCEPTION
        : esp32_mquickjs_future_call_and_wait(ctx,
                                              esp32_mquickjs_get_active_runtime(),
                                              *method,
                                              *rooted_receiver,
                                              argc,
                                              argv);
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &receiver_ref);
    return result;
}

#define FS_DIRECT_WRAPPER(function_name, method_name) \
    JSValue function_name(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) \
    { \
        return fs_future_call_and_wait(ctx, *this_val, method_name, argc, argv); \
    }

FS_DIRECT_WRAPPER(js_fs_list, "list")
FS_DIRECT_WRAPPER(js_fs_open, "open")
FS_DIRECT_WRAPPER(js_fs_stat, "stat")
FS_DIRECT_WRAPPER(js_fs_exists, "exists")
FS_DIRECT_WRAPPER(js_fs_readText, "readText")
FS_DIRECT_WRAPPER(js_fs_writeText, "writeText")
FS_DIRECT_WRAPPER(js_fs_appendText, "appendText")
FS_DIRECT_WRAPPER(js_fs_remove, "remove")
FS_DIRECT_WRAPPER(js_fs_rename, "rename")
FS_DIRECT_WRAPPER(js_fs_mkdir, "mkdir")

#undef FS_DIRECT_WRAPPER

#endif
