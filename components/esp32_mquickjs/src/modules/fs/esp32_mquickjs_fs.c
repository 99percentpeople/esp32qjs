#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_FS

#include "esp32_mquickjs_fs.h"

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "utils/esp32_mquickjs_fs_path.h"
#include "esp32_mquickjs_stream.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "esp32qjs";

#define ESP32_MQUICKJS_MAX_LITTLEFS_MOUNTS 4U
#define ESP32_MQUICKJS_PARTITION_LABEL_MAX 17U

typedef struct {
    bool active;
    char partition_label[ESP32_MQUICKJS_PARTITION_LABEL_MAX];
    char base_path[ESP32_MQUICKJS_FS_ROOT_MAX];
} esp32_mquickjs_littlefs_mount_t;

static bool s_littlefs_mounted;
static SemaphoreHandle_t s_fs_worker_lock;
static esp32_mquickjs_littlefs_mount_t
    s_littlefs_mounts[ESP32_MQUICKJS_MAX_LITTLEFS_MOUNTS];

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

static bool remember_littlefs_mount(const char *partition_label,
                                    const char *base_path)
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

static const char *active_fs_base_path(void)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();

    if (runtime != NULL) {
        if (runtime->load_root_depth > 0 && runtime->load_root[0] != '\0') {
            return runtime->load_root;
        }
        if (runtime->fs_root[0] != '\0') {
            return runtime->fs_root;
        }
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

    buf = heap_caps_malloc((size_t)file_size + 1, MALLOC_CAP_8BIT);
    if (buf == NULL) {
        fclose(file);
        return NULL;
    }

    read_len = fread(buf, 1, (size_t)file_size, file);
    fclose(file);
    if (read_len != (size_t)file_size) {
        heap_caps_free(buf);
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
                               JSValue value,
                               const char *api_name,
                               char *out_path,
                               size_t out_path_size)
{
    JSCStringBuf path_buf;
    const char *base_path = active_fs_base_path();
    const char *path;

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

bool esp32_mquickjs_mount_littlefs(bool format_if_mount_failed)
{
    if (s_littlefs_mounted) {
        return true;
    }
    s_littlefs_mounted = esp32_mquickjs_mount_littlefs_partition(
        ESP32_MQUICKJS_LITTLEFS_PARTITION_LABEL,
        ESP32_MQUICKJS_LITTLEFS_BASE_PATH,
        format_if_mount_failed);
    return s_littlefs_mounted;
}

bool esp32_mquickjs_mount_littlefs_partition(const char *partition_label,
                                             const char *base_path,
                                             bool format_if_mount_failed)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = base_path,
        .partition_label = partition_label,
        .format_if_mount_failed = format_if_mount_failed,
        .dont_mount = false,
    };
    esp_err_t ret;
    size_t total = 0;
    size_t used = 0;

    if (partition_label == NULL || partition_label[0] == '\0' ||
        base_path == NULL || base_path[0] != '/' ||
        strlen(partition_label) >= 17U ||
        strlen(base_path) >= ESP32_MQUICKJS_FS_ROOT_MAX ||
        strstr(base_path, "..") != NULL) {
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
                 "LittleFS '%s' mounted at %s: total=%u used=%u",
                 partition_label,
                 base_path,
                 (unsigned)total,
                 (unsigned)used);
    } else {
        ESP_LOGW(TAG, "LittleFS '%s' mounted but size query failed (%s)",
                 partition_label,
                 esp_err_to_name(ret));
    }
    if (!remember_littlefs_mount(partition_label, base_path)) {
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
    heap_caps_free(source);
    return result;
}

JSValue esp32_mquickjs_load_from_active_fs(JSContext *ctx,
                                           esp32_mquickjs_runtime_t *runtime,
                                           const char *script_path)
{
    return load_from_fs(ctx,
                        runtime,
                        active_fs_base_path(),
                        script_path);
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

    if (runtime == NULL || runtime->startup_bytecode != NULL) {
        return JS_ThrowInternalError(ctx, "startup bytecode is already loaded");
    }
    if (!esp32_mquickjs_fs_resolve_path(active_fs_base_path(),
                                        script_path,
                                        resolved_path,
                                        sizeof(resolved_path))) {
        return JS_ThrowTypeError(ctx,
                                 "startup script path must stay under %s",
                                 active_fs_base_path());
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

        heap_caps_free(source);
        return result;
    }

    if (JS_RelocateBytecode(ctx, source, (uint32_t)source_len) != 0) {
        heap_caps_free(source);
        return JS_ThrowInternalError(ctx, "failed to relocate startup bytecode");
    }
    compiled = JS_LoadBytecode(ctx, source);
    if (JS_IsException(compiled)) {
        heap_caps_free(source);
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
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewString(ctx, active_fs_base_path());
}

JSValue js_fs_set_root(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    JSCStringBuf path_buf;
    const char *path;

    (void)this_val;
    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "fs.setRoot(path) expects a mounted root path");
    }
    if (runtime == NULL) {
        return JS_ThrowInternalError(ctx, "JavaScript runtime is not active");
    }
    path = JS_ToCString(ctx, argv[0], &path_buf);
    if (!fs_root_is_available(path)) {
        return JS_ThrowRangeError(ctx, "fs.setRoot(path) requires an available root directory");
    }
    snprintf(runtime->fs_root, sizeof(runtime->fs_root), "%s", path);
    return JS_NewString(ctx, runtime->fs_root);
}

JSValue js_fs_info(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    const char *root = active_fs_base_path();
    esp32_mquickjs_littlefs_mount_t *mount;
    size_t total = 0;
    size_t used = 0;
    JSGCRef result_ref;
    JSValue *result;
    esp_err_t ret;

    (void)this_val;
    (void)argv;
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

JSValue js_fs_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    JSCStringBuf mode_buf;
    const char *mode = "r";

    (void)this_val;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs.open(path, mode?) expects a path");
    }
    if (js_value_to_fs_path(ctx, argv[0], "fs.open(path, mode?)", path, sizeof(path)) != 0) {
        return JS_EXCEPTION;
    }
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        if (!JS_IsString(ctx, argv[1])) {
            return JS_ThrowTypeError(ctx, "fs.open(path, mode) expects mode to be a string");
        }
        mode = JS_ToCString(ctx, argv[1], &mode_buf);
        if (mode == NULL) {
            return JS_EXCEPTION;
        }
    }

    {
        JSGCRef global_ref;
        JSValue *global_obj = JS_PushGCRef(ctx, &global_ref);
        JSValue result;

        *global_obj = JS_GetGlobalObject(ctx);
        if (JS_IsException(*global_obj)) {
            JS_PopGCRef(ctx, &global_ref);
            return JS_EXCEPTION;
        }
        result = esp32_mquickjs_stream_open_file(ctx, *global_obj, path, mode);
        JS_PopGCRef(ctx, &global_ref);
        return result;
    }
}

#define ESP32_MQUICKJS_FS_FUTURE_MAX_ENTRIES 64U

typedef enum {
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
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    char to_path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    char *data;
    size_t data_length;
    struct stat stat_value;
    fs_future_entry_t *entries;
    size_t entry_count;
    int error_number;
    bool result;
    volatile bool completed;
    bool cancelled;
};

static void fs_future_release(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    heap_caps_free(state->data);
    heap_caps_free(state->entries);
    heap_caps_free(state);
}

static bool fs_future_prepare_common(
    JSContext *ctx,
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
    api_name = kind == FS_FUTURE_LIST ? "fs.list(path?)" :
               kind == FS_FUTURE_STAT ? "fs.stat(path)" :
               kind == FS_FUTURE_EXISTS ? "fs.exists(path)" :
               kind == FS_FUTURE_READ_TEXT ? "fs.readText(path)" :
               kind == FS_FUTURE_WRITE_TEXT ? "fs.writeText(path, text)" :
               kind == FS_FUTURE_APPEND_TEXT ? "fs.appendText(path, text)" :
               kind == FS_FUTURE_REMOVE ? "fs.remove(path)" :
               kind == FS_FUTURE_RENAME ? "fs.rename(fromPath, toPath)" :
               "fs.mkdir(path)";
    if ((kind == FS_FUTURE_LIST && argc > 1) ||
        (kind != FS_FUTURE_LIST && kind != FS_FUTURE_RENAME &&
         kind != FS_FUTURE_WRITE_TEXT && kind != FS_FUTURE_APPEND_TEXT && argc != 1) ||
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
    state->kind = kind;
    if (kind == FS_FUTURE_LIST && argc == 0) {
        snprintf(state->path, sizeof(state->path), "%s", active_fs_base_path());
    } else if (js_value_to_fs_path(ctx, argv[0].val, api_name,
                                   state->path, sizeof(state->path)) != 0) {
        fs_future_release(state);
        return false;
    }
    if (kind == FS_FUTURE_RENAME &&
        js_value_to_fs_path(ctx, argv[1].val, api_name,
                            state->to_path, sizeof(state->to_path)) != 0) {
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
        state->data = heap_caps_malloc(state->data_length + 1U, MALLOC_CAP_8BIT);
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
        (void)this_ref; \
        return fs_future_prepare_common(ctx, kind_value, argc, argv, out_state); \
    }

FS_PREPARE(fs_list_future_prepare, FS_FUTURE_LIST)
FS_PREPARE(fs_stat_future_prepare, FS_FUTURE_STAT)
FS_PREPARE(fs_exists_future_prepare, FS_FUTURE_EXISTS)
FS_PREPARE(fs_read_text_future_prepare, FS_FUTURE_READ_TEXT)
FS_PREPARE(fs_write_text_future_prepare, FS_FUTURE_WRITE_TEXT)
FS_PREPARE(fs_append_text_future_prepare, FS_FUTURE_APPEND_TEXT)
FS_PREPARE(fs_remove_future_prepare, FS_FUTURE_REMOVE)
FS_PREPARE(fs_rename_future_prepare, FS_FUTURE_RENAME)
FS_PREPARE(fs_mkdir_future_prepare, FS_FUTURE_MKDIR)

#undef FS_PREPARE

static void fs_future_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;

    if (state == NULL) {
        return;
    }
    xSemaphoreTake(s_fs_worker_lock, portMAX_DELAY);
    errno = 0;
    if (state->kind == FS_FUTURE_LIST) {
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
                grown = heap_caps_realloc(state->entries,
                                          (state->entry_count + 1U) * sizeof(*state->entries),
                                          MALLOC_CAP_8BIT);
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
        state->data = (char *)load_script_file(state->path, &state->data_length);
        if (state->data == NULL) {
            state->error_number = errno != 0 ? errno : EIO;
        }
    } else if (state->kind == FS_FUTURE_WRITE_TEXT ||
               state->kind == FS_FUTURE_APPEND_TEXT) {
        FILE *file = fopen(state->path,
                           state->kind == FS_FUTURE_APPEND_TEXT ? "ab" : "wb");

        if (file == NULL) {
            state->error_number = errno;
        } else {
            size_t written = fwrite(state->data, 1, state->data_length, file);

            if (fclose(file) != 0 || written != state->data_length) {
                state->error_number = errno != 0 ? errno : EIO;
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
    xSemaphoreGive(s_fs_worker_lock);
    state->completed = true;
    (void)esp32_mquickjs_future_wake(state->runtime, state->token);
}

static bool fs_future_start(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime,
                            esp32_mquickjs_future_token_t token,
                            esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return false;
    }
    state->runtime = runtime;
    state->token = token;
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
    return state != NULL && state->completed
        ? ESP32_MQUICKJS_FUTURE_READY
        : ESP32_MQUICKJS_FUTURE_PENDING;
}

static const char *fs_future_action(const esp32_mquickjs_future_driver_state_t *state)
{
    return state->kind == FS_FUTURE_LIST ? "list()" :
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
        return fs_throw_error(ctx, fs_future_action(state), state->path,
                              state->error_number);
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

static bool fs_future_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->completed || state->cancelled) {
        return false;
    }
    state->cancelled = true;
    return true;
}

static void fs_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    fs_future_release(state);
}

#define FS_FUTURE_DRIVER(name, prepare_fn) \
    static const esp32_mquickjs_future_driver_t name = { \
        .prepare = prepare_fn, \
        .start = fs_future_start, \
        .poll = fs_future_poll, \
        .finish = fs_future_finish, \
        .cancel = fs_future_cancel, \
        .destroy = fs_future_destroy, \
    }

FS_FUTURE_DRIVER(s_fs_list_driver, fs_list_future_prepare);
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
        "list", "stat", "exists", "readText", "writeText",
        "appendText", "remove", "rename", "mkdir",
    };
    static const esp32_mquickjs_future_driver_t *drivers[] = {
        &s_fs_list_driver, &s_fs_stat_driver, &s_fs_exists_driver,
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

    if (s_fs_worker_lock == NULL) {
        s_fs_worker_lock = xSemaphoreCreateMutex();
        if (s_fs_worker_lock == NULL) {
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
    }
    global_obj = JS_PushGCRef(ctx, &global_ref);
    fs_obj = JS_PushGCRef(ctx, &fs_ref);
    *global_obj = JS_GetGlobalObject(ctx);
    *fs_obj = JS_IsException(*global_obj)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global_obj, "fs");
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
    return result;
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
