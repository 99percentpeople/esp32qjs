#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_FS

#include "esp32_mquickjs_fs.h"

#include "esp32_mquickjs_core.h"
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

static const char *TAG = "esp32qjs";

static bool s_littlefs_mounted;

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
    if (!esp32_mquickjs_fs_resolve_path(ESP32_MQUICKJS_LITTLEFS_BASE_PATH,
                                        path,
                                        out_path,
                                        out_path_size)) {
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
        JSGCRef entry_ref;
        JSValue *entry;
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

        entry = JS_PushGCRef(ctx, &entry_ref);
        *entry = fs_make_stat_object(ctx, entry_path, &st);
        if (JS_IsException(*entry) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *entries, index++, *entry))) {
            JS_PopGCRef(ctx, &entry_ref);
            goto fail;
        }
        JS_PopGCRef(ctx, &entry_ref);
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

void esp32_mquickjs_unmount_littlefs(void)
{
    if (!s_littlefs_mounted) {
        return;
    }
    esp_vfs_littlefs_unregister(ESP32_MQUICKJS_LITTLEFS_PARTITION_LABEL);
    s_littlefs_mounted = false;
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
    if (!esp32_mquickjs_fs_resolve_path(ESP32_MQUICKJS_LITTLEFS_BASE_PATH,
                                        script_path,
                                        resolved_path,
                                        sizeof(resolved_path))) {
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

JSValue js_fs_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    JSCStringBuf mode_buf;
    const char *mode = "r";

    (void)this_val;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs.open(path, mode?) expects a path");
    }
    if (js_value_to_littlefs_path(ctx, argv[0], "fs.open(path, mode?)", path, sizeof(path)) != 0) {
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

JSValue js_fs_list(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];

    (void)this_val;

    if (argc == 0) {
        return js_fs_list_path(ctx, ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
    }
    if (js_value_to_littlefs_path(ctx, argv[0], "fs.list(path)", path, sizeof(path)) != 0) {
        return JS_EXCEPTION;
    }
    return js_fs_list_path(ctx, path);
}

JSValue js_fs_stat(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];

    (void)this_val;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs.stat(path) expects a path");
    }
    if (js_value_to_littlefs_path(ctx, argv[0], "fs.stat(path)", path, sizeof(path)) != 0) {
        return JS_EXCEPTION;
    }
    return js_fs_stat_path(ctx, path);
}

JSValue js_fs_exists(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    struct stat st;

    (void)this_val;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs.exists(path) expects a path");
    }
    if (js_value_to_littlefs_path(ctx, argv[0], "fs.exists(path)", path, sizeof(path)) != 0) {
        return JS_EXCEPTION;
    }
    return JS_NewBool(stat(path, &st) == 0);
}

JSValue js_fs_readText(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];

    (void)this_val;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs.readText(path) expects a path");
    }
    if (js_value_to_littlefs_path(ctx, argv[0], "fs.readText(path)", path, sizeof(path)) != 0) {
        return JS_EXCEPTION;
    }
    return js_fs_read_text_path(ctx, path);
}

JSValue js_fs_writeText(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];

    (void)this_val;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "fs.writeText(path, text) expects a path and text");
    }
    if (js_value_to_littlefs_path(ctx, argv[0], "fs.writeText(path, text)", path, sizeof(path)) != 0) {
        return JS_EXCEPTION;
    }
    return js_fs_write_text_path(ctx, path, argv[1], false);
}

JSValue js_fs_appendText(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];

    (void)this_val;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "fs.appendText(path, text) expects a path and text");
    }
    if (js_value_to_littlefs_path(ctx, argv[0], "fs.appendText(path, text)", path, sizeof(path)) != 0) {
        return JS_EXCEPTION;
    }
    return js_fs_write_text_path(ctx, path, argv[1], true);
}

JSValue js_fs_remove(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    struct stat st;

    (void)this_val;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs.remove(path) expects a path");
    }
    if (js_value_to_littlefs_path(ctx, argv[0], "fs.remove(path)", path, sizeof(path)) != 0) {
        return JS_EXCEPTION;
    }
    if (stat(path, &st) != 0) {
        return fs_throw_errno(ctx, "remove()", path);
    }
    if ((S_ISDIR(st.st_mode) ? rmdir(path) : remove(path)) != 0) {
        return fs_throw_errno(ctx, "remove()", path);
    }
    return JS_NewBool(true);
}

JSValue js_fs_rename(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char from_path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    char to_path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];

    (void)this_val;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "fs.rename(fromPath, toPath) expects two paths");
    }
    if (js_value_to_littlefs_path(ctx,
                                  argv[0],
                                  "fs.rename(fromPath, toPath)",
                                  from_path,
                                  sizeof(from_path)) != 0 ||
        js_value_to_littlefs_path(ctx,
                                  argv[1],
                                  "fs.rename(fromPath, toPath)",
                                  to_path,
                                  sizeof(to_path)) != 0) {
        return JS_EXCEPTION;
    }
    if (rename(from_path, to_path) != 0) {
        return fs_throw_errno(ctx, "rename()", from_path);
    }
    return JS_NewBool(true);
}

JSValue js_fs_mkdir(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];

    (void)this_val;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs.mkdir(path) expects a path");
    }
    if (js_value_to_littlefs_path(ctx, argv[0], "fs.mkdir(path)", path, sizeof(path)) != 0) {
        return JS_EXCEPTION;
    }
    if (mkdir(path, 0777) != 0) {
        return fs_throw_errno(ctx, "mkdir()", path);
    }
    return JS_NewBool(true);
}

#endif
