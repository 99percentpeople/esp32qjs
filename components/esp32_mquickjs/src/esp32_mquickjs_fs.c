#include "esp32_mquickjs_internal.h"

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

static const char *TAG = "esp32qjs";

static bool s_littlefs_mounted;

static bool resolve_littlefs_path(const char *input_path, char *out_path, size_t out_path_size)
{
    static const size_t base_len = sizeof(ESP32_MQUICKJS_LITTLEFS_BASE_PATH) - 1;
    const char *cursor;
    size_t out_len;

    if (input_path == NULL || out_path == NULL || out_path_size <= base_len + 1) {
        return false;
    }

    memcpy(out_path, ESP32_MQUICKJS_LITTLEFS_BASE_PATH, base_len);
    out_path[base_len] = '\0';
    out_len = base_len;

    if (input_path[0] == '\0' || strcmp(input_path, ".") == 0) {
        return true;
    }

    if (input_path[0] == '/') {
        if (strncmp(input_path, ESP32_MQUICKJS_LITTLEFS_BASE_PATH, base_len) != 0) {
            return false;
        }
        if (input_path[base_len] != '\0' && input_path[base_len] != '/') {
            return false;
        }
        cursor = input_path + base_len;
    } else {
        cursor = input_path;
    }

    while (*cursor != '\0') {
        const char *segment_start;
        size_t segment_len;

        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        segment_start = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
        }
        segment_len = (size_t)(cursor - segment_start);

        if (segment_len == 1 && segment_start[0] == '.') {
            continue;
        }

        if (segment_len == 2 && segment_start[0] == '.' && segment_start[1] == '.') {
            char *slash;

            if (out_len == base_len) {
                return false;
            }

            slash = strrchr(out_path, '/');
            if (slash == NULL || (size_t)(slash - out_path) < base_len) {
                return false;
            }
            *slash = '\0';
            out_len = (size_t)(slash - out_path);
            continue;
        }

        if (out_len + 1 + segment_len >= out_path_size) {
            return false;
        }

        out_path[out_len++] = '/';
        memcpy(out_path + out_len, segment_start, segment_len);
        out_len += segment_len;
        out_path[out_len] = '\0';
    }

    return true;
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

static JSValue fs_throw_errno(JSContext *ctx, const char *action, const char *path)
{
    int err = errno;

    if (err == ENOENT) {
        return JS_ThrowReferenceError(ctx, "%s failed for %s (%s)", action, path, strerror(err));
    }
    return JS_ThrowInternalError(ctx, "%s failed for %s (%s)", action, path, strerror(err));
}

static const char *path_basename(const char *path)
{
    const char *slash;

    if (path == NULL || path[0] == '\0') {
        return "";
    }

    slash = strrchr(path, '/');
    if (slash == NULL) {
        return path;
    }
    return slash[1] == '\0' ? slash : slash + 1;
}

static int js_value_to_littlefs_path(JSContext *ctx,
                                     JSValue value,
                                     const char *api_name,
                                     char *out_path,
                                     size_t out_path_size)
{
    JSCStringBuf path_buf;
    const char *path;

    if (!JS_IsString(ctx, value)) {
        JS_ThrowTypeError(ctx, "%s expects a LittleFS path string", api_name);
        return -1;
    }

    path = JS_ToCString(ctx, value, &path_buf);
    if (!resolve_littlefs_path(path, out_path, out_path_size)) {
        JS_ThrowTypeError(ctx,
                          "%s expects a path under %s",
                          api_name,
                          ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
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
    if (!esp32_mquickjs_set_property(ctx, *entry, "name",
                                     JS_NewString(ctx, path_basename(path))) ||
        !esp32_mquickjs_set_property(ctx, *entry, "path",
                                     JS_NewString(ctx, path)) ||
        !esp32_mquickjs_set_property(ctx, *entry, "isDir",
                                     JS_NewBool(S_ISDIR(st->st_mode))) ||
        !esp32_mquickjs_set_property(ctx, *entry, "size",
                                     JS_NewInt64(ctx, (int64_t)st->st_size))) {
        goto fail;
    }

    return JS_PopGCRef(ctx, &entry_ref);

fail:
    JS_PopGCRef(ctx, &entry_ref);
    return JS_EXCEPTION;
}

static JSValue js_fs_stat_path(JSContext *ctx, const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        return fs_throw_errno(ctx, "stat()", path);
    }

    return fs_make_stat_object(ctx, path, &st);
}

static JSValue js_fs_list_path(JSContext *ctx, const char *path)
{
    DIR *dir = NULL;
    struct dirent *entry_raw;
    JSGCRef entries_ref;
    JSValue *entries;
    uint32_t index = 0;

    entries = JS_PushGCRef(ctx, &entries_ref);
    *entries = JS_NewArray(ctx, 0);
    if (JS_IsException(*entries)) {
        goto fail;
    }

    dir = opendir(path);
    if (dir == NULL) {
        JS_PopGCRef(ctx, &entries_ref);
        return fs_throw_errno(ctx, "list()", path);
    }

    while ((entry_raw = readdir(dir)) != NULL) {
        char entry_path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
        struct stat st;
        JSValue entry;
        int needed;

        if (strcmp(entry_raw->d_name, ".") == 0 || strcmp(entry_raw->d_name, "..") == 0) {
            continue;
        }

        needed = snprintf(entry_path, sizeof(entry_path), "%s/%s", path, entry_raw->d_name);
        if (needed <= 0 || (size_t)needed >= sizeof(entry_path)) {
            JS_ThrowInternalError(ctx, "path too long while listing %s", path);
            goto fail;
        }
        if (stat(entry_path, &st) != 0) {
            fs_throw_errno(ctx, "stat()", entry_path);
            goto fail;
        }

        entry = fs_make_stat_object(ctx, entry_path, &st);
        if (JS_IsException(entry)) {
            goto fail;
        }
        if (JS_IsException(JS_SetPropertyUint32(ctx, *entries, index++, entry))) {
            goto fail;
        }
    }

    closedir(dir);
    return JS_PopGCRef(ctx, &entries_ref);

fail:
    if (dir != NULL) {
        closedir(dir);
    }
    JS_PopGCRef(ctx, &entries_ref);
    return JS_EXCEPTION;
}

static JSValue js_fs_read_text_path(JSContext *ctx, const char *path)
{
    size_t source_len = 0;
    uint8_t *source = load_script_file(path, &source_len);
    JSValue result;

    if (source == NULL) {
        return fs_throw_errno(ctx, "readText()", path);
    }

    result = JS_NewStringLen(ctx, (const char *)source, source_len);
    heap_caps_free(source);
    return result;
}

static JSValue js_fs_write_text_path(JSContext *ctx, const char *path, JSValue text_value, bool append)
{
    JSCStringBuf text_buf;
    const char *text;
    size_t text_len = 0;
    FILE *file;
    size_t written;

    if (!JS_IsString(ctx, text_value)) {
        return JS_ThrowTypeError(ctx, "%s expects a text string",
                                 append ? "fs.appendText(path, text)" : "fs.writeText(path, text)");
    }

    text = JS_ToCStringLen(ctx, &text_len, text_value, &text_buf);
    file = fopen(path, append ? "ab" : "wb");
    if (file == NULL) {
        return fs_throw_errno(ctx, append ? "appendText()" : "writeText()", path);
    }

    written = fwrite(text, 1, text_len, file);
    if (fclose(file) != 0 || written != text_len) {
        return fs_throw_errno(ctx, append ? "appendText()" : "writeText()", path);
    }

    return JS_NewInt64(ctx, (int64_t)written);
}

bool esp32_mquickjs_mount_littlefs(bool format_if_mount_failed)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = ESP32_MQUICKJS_LITTLEFS_BASE_PATH,
        .partition_label = ESP32_MQUICKJS_LITTLEFS_PARTITION_LABEL,
        .format_if_mount_failed = format_if_mount_failed,
        .dont_mount = false,
    };
    esp_err_t ret;
    size_t total = 0;
    size_t used = 0;

    if (s_littlefs_mounted) {
        return true;
    }

    ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format LittleFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "LittleFS partition '%s' was not found",
                     ESP32_MQUICKJS_LITTLEFS_PARTITION_LABEL);
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return false;
    }

    ret = esp_littlefs_info(ESP32_MQUICKJS_LITTLEFS_PARTITION_LABEL, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG,
                 "LittleFS mounted at %s: total=%u used=%u",
                 ESP32_MQUICKJS_LITTLEFS_BASE_PATH,
                 (unsigned)total,
                 (unsigned)used);
    } else {
        ESP_LOGW(TAG, "LittleFS mounted but size query failed (%s)", esp_err_to_name(ret));
    }

    s_littlefs_mounted = true;
    return true;
}

JSValue esp32_mquickjs_load_from_littlefs(JSContext *ctx,
                                          esp32_mquickjs_runtime_t *runtime,
                                          const char *script_path)
{
    char resolved_path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    size_t source_len = 0;
    uint8_t *source;
    JSValue result;

    if (!s_littlefs_mounted) {
        return JS_ThrowInternalError(ctx,
                                     "LittleFS is not mounted at %s",
                                     ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
    }
    if (!resolve_littlefs_path(script_path, resolved_path, sizeof(resolved_path))) {
        return JS_ThrowTypeError(ctx, "load(path) expects a non-empty path under %s",
                                 ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
    }

    source = load_script_file(resolved_path, &source_len);
    if (source == NULL) {
        return JS_ThrowReferenceError(ctx, "failed to read script: %s", resolved_path);
    }

    result = esp32_mquickjs_eval(ctx, runtime, (const char *)source, resolved_path, 0);
    heap_caps_free(source);
    return result;
}

bool esp32_mquickjs_install_fs_module(JSContext *ctx, JSValue global_obj)
{
    JSGCRef module_ref;
    JSValue *module_obj;

    module_obj = JS_PushGCRef(ctx, &module_ref);
    *module_obj = JS_NewObject(ctx);
    if (JS_IsException(*module_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, *module_obj, "ROOT",
                                     JS_NewString(ctx, ESP32_MQUICKJS_LITTLEFS_BASE_PATH)) ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "open", "fs.open") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "list", "fs.list") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "stat", "fs.stat") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "exists", "fs.exists") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "readText", "fs.readText") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "writeText", "fs.writeText") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "appendText", "fs.appendText") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "remove", "fs.remove") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "rename", "fs.rename") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "mkdir", "fs.mkdir")) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, global_obj, "fs", JS_PopGCRef(ctx, &module_ref))) {
        return false;
    }
    return true;

fail:
    JS_PopGCRef(ctx, &module_ref);
    return false;
}

bool esp32_mquickjs_dispatch_fs(JSContext *ctx,
                                const char *operation,
                                int argc,
                                JSValue *argv,
                                JSValue *result)
{
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];

    if (strcmp(operation, "open") == 0) {
        JSCStringBuf mode_buf;
        const char *mode = "r";

        if (argc < 1) {
            *result = JS_ThrowTypeError(ctx, "fs.open(path, mode?) expects a path");
            return true;
        }
        if (js_value_to_littlefs_path(ctx, argv[0], "fs.open(path, mode?)", path, sizeof(path)) != 0) {
            *result = JS_EXCEPTION;
            return true;
        }
        if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
            if (!JS_IsString(ctx, argv[1])) {
                *result = JS_ThrowTypeError(ctx, "fs.open(path, mode) expects mode to be a string");
                return true;
            }
            mode = JS_ToCString(ctx, argv[1], &mode_buf);
            if (mode == NULL) {
                *result = JS_EXCEPTION;
                return true;
            }
        }

        {
            JSGCRef global_ref;
            JSValue *global_obj = JS_PushGCRef(ctx, &global_ref);
            *global_obj = JS_GetGlobalObject(ctx);
            if (JS_IsException(*global_obj)) {
                JS_PopGCRef(ctx, &global_ref);
                *result = JS_EXCEPTION;
                return true;
            }
            *result = esp32_mquickjs_stream_open_file(ctx, *global_obj, path, mode);
            JS_PopGCRef(ctx, &global_ref);
        }
        return true;
    }

    if (strcmp(operation, "list") == 0) {
        if (argc == 0) {
            *result = js_fs_list_path(ctx, ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
            return true;
        }
        if (js_value_to_littlefs_path(ctx, argv[0], "fs.list(path)", path, sizeof(path)) != 0) {
            *result = JS_EXCEPTION;
            return true;
        }
        *result = js_fs_list_path(ctx, path);
        return true;
    }

    if (strcmp(operation, "stat") == 0) {
        if (argc < 1) {
            *result = JS_ThrowTypeError(ctx, "fs.stat(path) expects a path");
            return true;
        }
        if (js_value_to_littlefs_path(ctx, argv[0], "fs.stat(path)", path, sizeof(path)) != 0) {
            *result = JS_EXCEPTION;
            return true;
        }
        *result = js_fs_stat_path(ctx, path);
        return true;
    }

    if (strcmp(operation, "exists") == 0) {
        struct stat st;

        if (argc < 1) {
            *result = JS_ThrowTypeError(ctx, "fs.exists(path) expects a path");
            return true;
        }
        if (js_value_to_littlefs_path(ctx, argv[0], "fs.exists(path)", path, sizeof(path)) != 0) {
            *result = JS_EXCEPTION;
            return true;
        }
        *result = JS_NewBool(stat(path, &st) == 0);
        return true;
    }

    if (strcmp(operation, "readText") == 0) {
        if (argc < 1) {
            *result = JS_ThrowTypeError(ctx, "fs.readText(path) expects a path");
            return true;
        }
        if (js_value_to_littlefs_path(ctx, argv[0], "fs.readText(path)", path, sizeof(path)) != 0) {
            *result = JS_EXCEPTION;
            return true;
        }
        *result = js_fs_read_text_path(ctx, path);
        return true;
    }

    if (strcmp(operation, "writeText") == 0 || strcmp(operation, "appendText") == 0) {
        bool append = (strcmp(operation, "appendText") == 0);

        if (argc < 2) {
            *result = JS_ThrowTypeError(ctx,
                                        append ? "fs.appendText(path, text) expects a path and text"
                                               : "fs.writeText(path, text) expects a path and text");
            return true;
        }
        if (js_value_to_littlefs_path(ctx,
                                      argv[0],
                                      append ? "fs.appendText(path, text)" : "fs.writeText(path, text)",
                                      path,
                                      sizeof(path)) != 0) {
            *result = JS_EXCEPTION;
            return true;
        }
        *result = js_fs_write_text_path(ctx, path, argv[1], append);
        return true;
    }

    if (strcmp(operation, "remove") == 0) {
        struct stat st;

        if (argc < 1) {
            *result = JS_ThrowTypeError(ctx, "fs.remove(path) expects a path");
            return true;
        }
        if (js_value_to_littlefs_path(ctx, argv[0], "fs.remove(path)", path, sizeof(path)) != 0) {
            *result = JS_EXCEPTION;
            return true;
        }
        if (stat(path, &st) != 0) {
            *result = fs_throw_errno(ctx, "remove()", path);
            return true;
        }
        if ((S_ISDIR(st.st_mode) ? rmdir(path) : remove(path)) != 0) {
            *result = fs_throw_errno(ctx, "remove()", path);
            return true;
        }
        *result = JS_NewBool(true);
        return true;
    }

    if (strcmp(operation, "rename") == 0) {
        char to_path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];

        if (argc < 2) {
            *result = JS_ThrowTypeError(ctx, "fs.rename(fromPath, toPath) expects two paths");
            return true;
        }
        if (js_value_to_littlefs_path(ctx,
                                      argv[0],
                                      "fs.rename(fromPath, toPath)",
                                      path,
                                      sizeof(path)) != 0 ||
            js_value_to_littlefs_path(ctx,
                                      argv[1],
                                      "fs.rename(fromPath, toPath)",
                                      to_path,
                                      sizeof(to_path)) != 0) {
            *result = JS_EXCEPTION;
            return true;
        }
        if (rename(path, to_path) != 0) {
            *result = fs_throw_errno(ctx, "rename()", path);
            return true;
        }
        *result = JS_NewBool(true);
        return true;
    }

    if (strcmp(operation, "mkdir") == 0) {
        if (argc < 1) {
            *result = JS_ThrowTypeError(ctx, "fs.mkdir(path) expects a path");
            return true;
        }
        if (js_value_to_littlefs_path(ctx, argv[0], "fs.mkdir(path)", path, sizeof(path)) != 0) {
            *result = JS_EXCEPTION;
            return true;
        }
        if (mkdir(path, 0777) != 0) {
            *result = fs_throw_errno(ctx, "mkdir()", path);
            return true;
        }
        *result = JS_NewBool(true);
        return true;
    }

    return false;
}
