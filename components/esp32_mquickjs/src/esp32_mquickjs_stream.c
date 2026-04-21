#include "esp32_mquickjs_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"

#define ESP32_MQUICKJS_MAX_STREAMS 16
#define ESP32_MQUICKJS_STREAM_READ_CHUNK_DEFAULT 1024
#define ESP32_MQUICKJS_STREAM_READ_ALL_CHUNK 1024

typedef enum {
    ESP32_MQUICKJS_STREAM_KIND_FILE = 0,
    ESP32_MQUICKJS_STREAM_KIND_MEMORY = 1,
} esp32_mquickjs_stream_kind_t;

typedef struct {
    uint8_t stream_id;
    bool allocated;
    uint32_t generation;
    esp32_mquickjs_stream_kind_t kind;
    char mode[8];
    bool readable;
    bool writable;
    bool seekable;
    char *path;
    union {
        FILE *file;
        struct {
            uint8_t *data;
            size_t len;
            size_t pos;
            bool owned;
        } memory;
    } handle;
} esp32_mquickjs_stream_slot_t;

static bool s_streams_initialized;
static esp32_mquickjs_stream_slot_t s_streams[ESP32_MQUICKJS_MAX_STREAMS];

static void stream_init_once(void)
{
    int i;

    if (s_streams_initialized) {
        return;
    }
    memset(s_streams, 0, sizeof(s_streams));
    for (i = 0; i < ESP32_MQUICKJS_MAX_STREAMS; ++i) {
        s_streams[i].stream_id = (uint8_t)i;
    }
    s_streams_initialized = true;
}

static const char *stream_kind_name(esp32_mquickjs_stream_kind_t kind)
{
    return kind == ESP32_MQUICKJS_STREAM_KIND_MEMORY ? "memory" : "file";
}

static esp32_mquickjs_stream_slot_t *stream_alloc_slot(void)
{
    int i;

    stream_init_once();
    for (i = 0; i < ESP32_MQUICKJS_MAX_STREAMS; ++i) {
        if (!s_streams[i].allocated) {
            s_streams[i].allocated = true;
            s_streams[i].generation++;
            return &s_streams[i];
        }
    }
    return NULL;
}

static esp32_mquickjs_stream_slot_t *stream_get_slot(const esp32_mquickjs_fs_stream_ref_t *ref)
{
    esp32_mquickjs_stream_slot_t *slot;

    if (ref == NULL || ref->stream_id < 0 || ref->stream_id >= ESP32_MQUICKJS_MAX_STREAMS) {
        return NULL;
    }

    slot = &s_streams[ref->stream_id];
    if (!slot->allocated || slot->generation != ref->generation) {
        return NULL;
    }
    return slot;
}

static void stream_cleanup_slot(esp32_mquickjs_stream_slot_t *slot)
{
    if (slot == NULL) {
        return;
    }

    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_FILE && slot->handle.file != NULL) {
        fclose(slot->handle.file);
        slot->handle.file = NULL;
    } else if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_MEMORY && slot->handle.memory.owned) {
        heap_caps_free(slot->handle.memory.data);
        slot->handle.memory.data = NULL;
    }

    heap_caps_free(slot->path);
    slot->path = NULL;
    slot->allocated = false;
    slot->readable = false;
    slot->writable = false;
    slot->seekable = false;
    slot->kind = ESP32_MQUICKJS_STREAM_KIND_FILE;
    memset(slot->mode, 0, sizeof(slot->mode));
    memset(&slot->handle, 0, sizeof(slot->handle));
}

static bool stream_parse_mode(const char *mode, bool *out_readable, bool *out_writable)
{
    if (mode == NULL || out_readable == NULL || out_writable == NULL) {
        return false;
    }

    *out_readable = false;
    *out_writable = false;

    if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0) {
        *out_readable = true;
        return true;
    }
    if (strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0) {
        *out_writable = true;
        return true;
    }
    if (strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0) {
        *out_writable = true;
        return true;
    }
    if (strcmp(mode, "r+") == 0 || strcmp(mode, "rb+") == 0 || strcmp(mode, "r+b") == 0) {
        *out_readable = true;
        *out_writable = true;
        return true;
    }
    if (strcmp(mode, "w+") == 0 || strcmp(mode, "wb+") == 0 || strcmp(mode, "w+b") == 0) {
        *out_readable = true;
        *out_writable = true;
        return true;
    }
    if (strcmp(mode, "a+") == 0 || strcmp(mode, "ab+") == 0 || strcmp(mode, "a+b") == 0) {
        *out_readable = true;
        *out_writable = true;
        return true;
    }
    return false;
}

static int stream_ref_from_object(JSContext *ctx,
                                  JSValue value,
                                  const char *api_name,
                                  esp32_mquickjs_fs_stream_ref_t *out_ref)
{
    JSGCRef id_ref;
    JSGCRef generation_ref;
    JSValue *id_value;
    JSValue *generation_value;
    int stream_id;
    uint32_t generation;

    if (out_ref == NULL) {
        return -1;
    }
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        JS_ThrowTypeError(ctx, "%s expects a Stream object", api_name);
        return -1;
    }

    id_value = JS_PushGCRef(ctx, &id_ref);
    generation_value = JS_PushGCRef(ctx, &generation_ref);
    *id_value = JS_GetPropertyStr(ctx, value, ESP32_MQUICKJS_FS_STREAM_ID_KEY);
    *generation_value = JS_GetPropertyStr(ctx, value, ESP32_MQUICKJS_FS_STREAM_GENERATION_KEY);
    if (JS_IsException(*id_value) || JS_IsException(*generation_value)) {
        JS_PopGCRef(ctx, &generation_ref);
        JS_PopGCRef(ctx, &id_ref);
        return -1;
    }
    if (JS_ToInt32(ctx, &stream_id, *id_value) != 0 || JS_ToUint32(ctx, &generation, *generation_value) != 0) {
        JS_PopGCRef(ctx, &generation_ref);
        JS_PopGCRef(ctx, &id_ref);
        JS_ThrowTypeError(ctx, "%s expects a Stream object", api_name);
        return -1;
    }

    JS_PopGCRef(ctx, &generation_ref);
    JS_PopGCRef(ctx, &id_ref);
    out_ref->stream_id = stream_id;
    out_ref->generation = generation;
    return 0;
}

static int stream_ref_try_from_object(JSContext *ctx,
                                      JSValue value,
                                      esp32_mquickjs_fs_stream_ref_t *out_ref)
{
    JSGCRef id_ref;
    JSGCRef generation_ref;
    JSValue *id_value;
    JSValue *generation_value;
    int stream_id;
    uint32_t generation;

    if (out_ref == NULL || JS_IsUndefined(value) || JS_IsNull(value)) {
        return -1;
    }

    id_value = JS_PushGCRef(ctx, &id_ref);
    generation_value = JS_PushGCRef(ctx, &generation_ref);
    *id_value = JS_GetPropertyStr(ctx, value, ESP32_MQUICKJS_FS_STREAM_ID_KEY);
    *generation_value = JS_GetPropertyStr(ctx, value, ESP32_MQUICKJS_FS_STREAM_GENERATION_KEY);
    if (JS_IsException(*id_value) || JS_IsException(*generation_value) ||
        JS_ToInt32(ctx, &stream_id, *id_value) != 0 ||
        JS_ToUint32(ctx, &generation, *generation_value) != 0) {
        JS_PopGCRef(ctx, &generation_ref);
        JS_PopGCRef(ctx, &id_ref);
        return -1;
    }

    JS_PopGCRef(ctx, &generation_ref);
    JS_PopGCRef(ctx, &id_ref);
    out_ref->stream_id = stream_id;
    out_ref->generation = generation;
    return 0;
}

static int stream_get_bound_ref(JSContext *ctx,
                                JSValue bound_value,
                                const char *api_name,
                                esp32_mquickjs_fs_stream_ref_t *out_ref,
                                esp32_mquickjs_stream_slot_t **out_slot)
{
    esp32_mquickjs_stream_slot_t *slot;

    if (stream_ref_from_object(ctx, bound_value, api_name, out_ref) != 0) {
        return -1;
    }
    slot = stream_get_slot(out_ref);
    if (slot == NULL) {
        JS_ThrowReferenceError(ctx, "%s failed because the stream is closed", api_name);
        return -1;
    }
    if (out_slot != NULL) {
        *out_slot = slot;
    }
    return 0;
}

static JSValue stream_make_object(JSContext *ctx,
                                  JSValue global_obj,
                                  const esp32_mquickjs_stream_slot_t *slot)
{
    JSGCRef stream_ref;
    JSGCRef context_ref;
    JSGCRef stream_dup_ref;
    JSValue *stream_obj;
    JSValue *context_obj;
    JSValue *stream_dup_obj;

    stream_obj = JS_PushGCRef(ctx, &stream_ref);
    context_obj = JS_PushGCRef(ctx, &context_ref);
    stream_dup_obj = JS_PushGCRef(ctx, &stream_dup_ref);
    *stream_obj = JS_NewObject(ctx);
    *context_obj = JS_NewObject(ctx);
    *stream_dup_obj = JS_UNDEFINED;
    if (JS_IsException(*stream_obj) || JS_IsException(*context_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, *context_obj, ESP32_MQUICKJS_FS_STREAM_ID_KEY,
                                     JS_NewInt32(ctx, slot->stream_id)) ||
        !esp32_mquickjs_set_property(ctx, *context_obj, ESP32_MQUICKJS_FS_STREAM_GENERATION_KEY,
                                     JS_NewUint32(ctx, slot->generation)) ||
        !esp32_mquickjs_set_property(ctx, *stream_obj, "kind",
                                     JS_NewString(ctx, stream_kind_name(slot->kind))) ||
        !esp32_mquickjs_set_property(ctx, *stream_obj, "mode",
                                     JS_NewString(ctx, slot->mode[0] != '\0' ? slot->mode : "r")) ||
        !esp32_mquickjs_set_property(ctx, *stream_obj, "path",
                                     JS_NewString(ctx, slot->path != NULL ? slot->path : "")) ||
        !esp32_mquickjs_set_property(ctx, *stream_obj, "readable",
                                     JS_NewBool(slot->readable)) ||
        !esp32_mquickjs_set_property(ctx, *stream_obj, "writable",
                                     JS_NewBool(slot->writable)) ||
        !esp32_mquickjs_set_property(ctx, *stream_obj, "seekable",
                                     JS_NewBool(slot->seekable)) ||
        !esp32_mquickjs_set_property(ctx, *stream_obj, ESP32_MQUICKJS_FS_STREAM_ID_KEY,
                                     JS_NewInt32(ctx, slot->stream_id)) ||
        !esp32_mquickjs_set_property(ctx, *stream_obj, ESP32_MQUICKJS_FS_STREAM_GENERATION_KEY,
                                     JS_NewUint32(ctx, slot->generation))) {
        goto fail;
    }

    *stream_dup_obj = *context_obj;
    if (!esp32_mquickjs_set_bound_bridge_function_with_arg(ctx,
                                                           *stream_obj,
                                                           global_obj,
                                                           "read",
                                                           "stream.read",
                                                           *stream_dup_obj) ||
        !esp32_mquickjs_set_bound_bridge_function_with_arg(ctx,
                                                           *stream_obj,
                                                           global_obj,
                                                           "write",
                                                           "stream.write",
                                                           *context_obj) ||
        !esp32_mquickjs_set_bound_bridge_function_with_arg(ctx,
                                                           *stream_obj,
                                                           global_obj,
                                                           "flush",
                                                           "stream.flush",
                                                           *context_obj) ||
        !esp32_mquickjs_set_bound_bridge_function_with_arg(ctx,
                                                           *stream_obj,
                                                           global_obj,
                                                           "close",
                                                           "stream.close",
                                                           *context_obj) ||
        !esp32_mquickjs_set_bound_bridge_function_with_arg(ctx,
                                                           *stream_obj,
                                                           global_obj,
                                                           "seek",
                                                           "stream.seek",
                                                           *context_obj) ||
        !esp32_mquickjs_set_bound_bridge_function_with_arg(ctx,
                                                           *stream_obj,
                                                           global_obj,
                                                           "tell",
                                                           "stream.tell",
                                                           *context_obj) ||
        !esp32_mquickjs_set_bound_bridge_function_with_arg(ctx,
                                                           *stream_obj,
                                                           global_obj,
                                                           "eof",
                                                           "stream.eof",
                                                           *context_obj)) {
        goto fail;
    }

    JS_PopGCRef(ctx, &stream_dup_ref);
    JS_PopGCRef(ctx, &context_ref);
    return JS_PopGCRef(ctx, &stream_ref);

fail:
    JS_PopGCRef(ctx, &stream_dup_ref);
    JS_PopGCRef(ctx, &context_ref);
    JS_PopGCRef(ctx, &stream_ref);
    return JS_EXCEPTION;
}

static int stream_open_memory_owned_slot(char *data,
                                         size_t data_len,
                                         esp32_mquickjs_stream_slot_t **out_slot)
{
    esp32_mquickjs_stream_slot_t *slot = stream_alloc_slot();

    if (slot == NULL) {
        return -1;
    }

    slot->kind = ESP32_MQUICKJS_STREAM_KIND_MEMORY;
    slot->readable = true;
    slot->writable = false;
    slot->seekable = true;
    memcpy(slot->mode, "r", 2);
    slot->handle.memory.data = (uint8_t *)data;
    slot->handle.memory.len = data_len;
    slot->handle.memory.pos = 0;
    slot->handle.memory.owned = true;
    if (out_slot != NULL) {
        *out_slot = slot;
    }
    return 0;
}

static int stream_slot_read(esp32_mquickjs_stream_slot_t *slot,
                            void *buf,
                            size_t buf_len,
                            size_t *out_len)
{
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (slot == NULL || buf == NULL || buf_len == 0 || out_len == NULL || !slot->readable) {
        return -1;
    }

    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_FILE) {
        size_t read_len = fread(buf, 1, buf_len, slot->handle.file);

        if (read_len == 0 && ferror(slot->handle.file) != 0) {
            return -1;
        }
        *out_len = read_len;
        return 0;
    }

    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_MEMORY) {
        size_t available = slot->handle.memory.len > slot->handle.memory.pos
                               ? slot->handle.memory.len - slot->handle.memory.pos
                               : 0;
        size_t read_len = available < buf_len ? available : buf_len;

        if (read_len > 0) {
            memcpy(buf, slot->handle.memory.data + slot->handle.memory.pos, read_len);
            slot->handle.memory.pos += read_len;
        }
        *out_len = read_len;
        return 0;
    }

    return -1;
}

static int stream_slot_seek(esp32_mquickjs_stream_slot_t *slot,
                            int offset,
                            int whence,
                            int64_t *out_pos)
{
    if (slot == NULL || !slot->seekable || out_pos == NULL) {
        return -1;
    }

    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_FILE) {
        long pos;

        if (fseek(slot->handle.file, (long)offset, whence) != 0) {
            return -1;
        }
        pos = ftell(slot->handle.file);
        if (pos < 0) {
            return -1;
        }
        *out_pos = (int64_t)pos;
        return 0;
    }

    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_MEMORY) {
        int64_t base = 0;
        int64_t pos = 0;

        if (whence == SEEK_CUR) {
            base = (int64_t)slot->handle.memory.pos;
        } else if (whence == SEEK_END) {
            base = (int64_t)slot->handle.memory.len;
        } else if (whence != SEEK_SET) {
            return -1;
        }

        pos = base + (int64_t)offset;
        if (pos < 0 || (size_t)pos > slot->handle.memory.len) {
            return -1;
        }
        slot->handle.memory.pos = (size_t)pos;
        *out_pos = pos;
        return 0;
    }

    return -1;
}

static int64_t stream_slot_tell(esp32_mquickjs_stream_slot_t *slot)
{
    if (slot == NULL) {
        return -1;
    }
    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_FILE) {
        long pos = ftell(slot->handle.file);
        return pos < 0 ? -1 : (int64_t)pos;
    }
    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_MEMORY) {
        return (int64_t)slot->handle.memory.pos;
    }
    return -1;
}

static bool stream_slot_eof(esp32_mquickjs_stream_slot_t *slot)
{
    if (slot == NULL) {
        return true;
    }
    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_FILE) {
        return feof(slot->handle.file) != 0;
    }
    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_MEMORY) {
        return slot->handle.memory.pos >= slot->handle.memory.len;
    }
    return true;
}

bool esp32_mquickjs_install_stream_module(JSContext *ctx, JSValue global_obj)
{
    JSGCRef stream_ref;
    JSValue *stream_obj;

    stream_obj = JS_PushGCRef(ctx, &stream_ref);
    *stream_obj = JS_NewObject(ctx);
    if (JS_IsException(*stream_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, *stream_obj, "SEEK_SET", JS_NewInt32(ctx, SEEK_SET)) ||
        !esp32_mquickjs_set_property(ctx, *stream_obj, "SEEK_CUR", JS_NewInt32(ctx, SEEK_CUR)) ||
        !esp32_mquickjs_set_property(ctx, *stream_obj, "SEEK_END", JS_NewInt32(ctx, SEEK_END))) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, global_obj, "Stream", JS_PopGCRef(ctx, &stream_ref))) {
        return false;
    }
    return true;

fail:
    JS_PopGCRef(ctx, &stream_ref);
    return false;
}

JSValue esp32_mquickjs_stream_open_file(JSContext *ctx,
                                        JSValue global_obj,
                                        const char *path,
                                        const char *mode)
{
    esp32_mquickjs_stream_slot_t *slot;
    JSValue result;

    slot = stream_alloc_slot();
    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "too many open streams");
    }
    if (!stream_parse_mode(mode, &slot->readable, &slot->writable)) {
        slot->allocated = false;
        return JS_ThrowTypeError(ctx, "unsupported stream mode: %s", mode);
    }

    slot->kind = ESP32_MQUICKJS_STREAM_KIND_FILE;
    slot->seekable = true;
    slot->handle.file = fopen(path, mode);
    if (slot->handle.file == NULL) {
        slot->allocated = false;
        return JS_ThrowInternalError(ctx, "open() failed for %s", path);
    }
    slot->path = heap_caps_malloc(strlen(path) + 1, MALLOC_CAP_8BIT);
    if (slot->path == NULL) {
        stream_cleanup_slot(slot);
        return JS_ThrowOutOfMemory(ctx);
    }
    memcpy(slot->path, path, strlen(path) + 1);
    memcpy(slot->mode, mode, strlen(mode) + 1);

    result = stream_make_object(ctx, global_obj, slot);
    if (JS_IsException(result)) {
        stream_cleanup_slot(slot);
    }
    return result;
}

JSValue esp32_mquickjs_stream_open_memory_owned(JSContext *ctx,
                                                JSValue global_obj,
                                                char *data,
                                                size_t data_len)
{
    esp32_mquickjs_stream_slot_t *slot;
    JSValue result;

    if (data == NULL && data_len != 0) {
        return JS_ThrowInternalError(ctx, "memory stream requires valid data");
    }
    if (stream_open_memory_owned_slot(data, data_len, &slot) != 0) {
        heap_caps_free(data);
        return JS_ThrowInternalError(ctx, "too many open streams");
    }

    result = stream_make_object(ctx, global_obj, slot);
    if (JS_IsException(result)) {
        stream_cleanup_slot(slot);
    }
    return result;
}

JSValue esp32_mquickjs_stream_clone(JSContext *ctx, JSValue global_obj, JSValue stream_value)
{
    esp32_mquickjs_fs_stream_ref_t ref;
    esp32_mquickjs_stream_slot_t *slot;

    if (stream_ref_from_object(ctx, stream_value, "Stream", &ref) != 0) {
        return JS_EXCEPTION;
    }
    slot = stream_get_slot(&ref);
    if (slot == NULL) {
        return JS_ThrowReferenceError(ctx, "stream is closed");
    }
    return stream_make_object(ctx, global_obj, slot);
}

bool esp32_mquickjs_fs_parse_stream_ref(JSContext *ctx,
                                        JSValue stream_value,
                                        esp32_mquickjs_fs_stream_ref_t *out_ref)
{
    if (stream_ref_from_object(ctx, stream_value, "Stream", out_ref) != 0) {
        return false;
    }
    if (stream_get_slot(out_ref) == NULL) {
        JS_ThrowReferenceError(ctx, "Stream expects an open stream");
        return false;
    }
    return true;
}

bool esp32_mquickjs_stream_is_stream(JSContext *ctx, JSValue stream_value)
{
    esp32_mquickjs_fs_stream_ref_t ref;

    return stream_ref_from_object(ctx, stream_value, "Stream", &ref) == 0 && stream_get_slot(&ref) != NULL;
}

int esp32_mquickjs_stream_read_all_text(JSContext *ctx,
                                        JSValue stream_value,
                                        const char *api_name,
                                        char **out_text,
                                        size_t *out_len)
{
    esp32_mquickjs_fs_stream_ref_t ref;
    esp32_mquickjs_stream_slot_t *slot;
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int result = -1;

    if (out_text == NULL || out_len == NULL) {
        return -1;
    }
    *out_text = NULL;
    *out_len = 0;

    if (stream_ref_from_object(ctx, stream_value, api_name, &ref) != 0) {
        return -1;
    }
    slot = stream_get_slot(&ref);
    if (slot == NULL || !slot->readable) {
        JS_ThrowTypeError(ctx, "%s expects a readable Stream", api_name);
        return -1;
    }

    for (;;) {
        uint8_t chunk[ESP32_MQUICKJS_STREAM_READ_ALL_CHUNK];
        size_t read_len = 0;

        if (stream_slot_read(slot, chunk, sizeof(chunk), &read_len) != 0) {
            JS_ThrowInternalError(ctx, "%s failed while reading the stream", api_name);
            goto done;
        }
        if (read_len == 0) {
            break;
        }
        if (capacity < length + read_len + 1) {
            size_t new_capacity = capacity == 0 ? 1024 : capacity;

            while (new_capacity < length + read_len + 1) {
                new_capacity *= 2;
            }
            buffer = heap_caps_realloc(buffer, new_capacity, MALLOC_CAP_8BIT);
            if (buffer == NULL) {
                JS_ThrowOutOfMemory(ctx);
                goto done;
            }
            capacity = new_capacity;
        }
        memcpy(buffer + length, chunk, read_len);
        length += read_len;
    }

    if (buffer == NULL) {
        buffer = heap_caps_malloc(1, MALLOC_CAP_8BIT);
        if (buffer == NULL) {
            JS_ThrowOutOfMemory(ctx);
            goto done;
        }
    }
    buffer[length] = '\0';
    *out_text = buffer;
    *out_len = length;
    buffer = NULL;
    result = 0;

done:
    heap_caps_free(buffer);
    return result;
}

esp_err_t esp32_mquickjs_stream_close_value(JSContext *ctx, JSValue stream_value)
{
    esp32_mquickjs_fs_stream_ref_t ref;

    if (stream_ref_try_from_object(ctx, stream_value, &ref) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp32_mquickjs_fs_stream_close(&ref);
}

esp_err_t esp32_mquickjs_fs_stream_read(const esp32_mquickjs_fs_stream_ref_t *ref,
                                        void *buf,
                                        size_t buf_len,
                                        size_t *out_len)
{
    esp32_mquickjs_stream_slot_t *slot;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (buf == NULL || buf_len == 0 || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    slot = stream_get_slot(ref);
    if (stream_slot_read(slot, buf, buf_len, out_len) != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t esp32_mquickjs_fs_stream_close(const esp32_mquickjs_fs_stream_ref_t *ref)
{
    esp32_mquickjs_stream_slot_t *slot;

    slot = stream_get_slot(ref);
    if (slot == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    stream_cleanup_slot(slot);
    return ESP_OK;
}

bool esp32_mquickjs_dispatch_stream(JSContext *ctx,
                                    const char *operation,
                                    int argc,
                                    JSValue *argv,
                                    JSValue *result)
{
    esp32_mquickjs_fs_stream_ref_t stream_ref;
    esp32_mquickjs_stream_slot_t *slot = NULL;

    if (argc < 1) {
        *result = JS_ThrowTypeError(ctx, "Stream operation is missing its target");
        return true;
    }
    if (stream_get_bound_ref(ctx, argv[0], operation, &stream_ref, &slot) != 0) {
        *result = JS_EXCEPTION;
        return true;
    }

    if (strcmp(operation, "close") == 0) {
        stream_cleanup_slot(slot);
        *result = JS_TRUE;
        return true;
    }

    if (strcmp(operation, "flush") == 0) {
        if (!slot->writable) {
            *result = JS_ThrowTypeError(ctx, "stream.flush() requires a writable stream");
            return true;
        }
        if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_MEMORY) {
            *result = JS_TRUE;
            return true;
        }
        if (fflush(slot->handle.file) != 0) {
            *result = JS_ThrowInternalError(ctx, "stream.flush() failed for %s",
                                            slot->path != NULL ? slot->path : "<stream>");
            return true;
        }
        *result = JS_TRUE;
        return true;
    }

    if (strcmp(operation, "tell") == 0) {
        int64_t pos = stream_slot_tell(slot);

        if (pos < 0) {
            *result = JS_ThrowInternalError(ctx, "stream.tell() failed");
            return true;
        }
        *result = JS_NewInt64(ctx, pos);
        return true;
    }

    if (strcmp(operation, "seek") == 0) {
        int offset = 0;
        int whence = SEEK_SET;
        int64_t pos = 0;

        if (argc < 2 || JS_ToInt32(ctx, &offset, argv[1]) != 0) {
            *result = JS_ThrowTypeError(ctx, "stream.seek(offset, whence?) expects an integer offset");
            return true;
        }
        if (argc >= 3 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2]) &&
            JS_ToInt32(ctx, &whence, argv[2]) != 0) {
            *result = JS_ThrowTypeError(ctx, "stream.seek(offset, whence) expects a valid whence");
            return true;
        }
        if (stream_slot_seek(slot, offset, whence, &pos) != 0) {
            *result = JS_ThrowInternalError(ctx, "stream.seek() failed");
            return true;
        }
        *result = JS_NewInt64(ctx, pos);
        return true;
    }

    if (strcmp(operation, "eof") == 0) {
        *result = JS_NewBool(stream_slot_eof(slot));
        return true;
    }

    if (strcmp(operation, "read") == 0) {
        int chunk_size = ESP32_MQUICKJS_STREAM_READ_CHUNK_DEFAULT;
        char *buf;
        size_t read_len = 0;

        if (!slot->readable) {
            *result = JS_ThrowTypeError(ctx, "stream.read(size?) requires a readable stream");
            return true;
        }
        if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]) &&
            JS_ToInt32(ctx, &chunk_size, argv[1]) != 0) {
            *result = JS_ThrowTypeError(ctx, "stream.read(size) expects a positive integer");
            return true;
        }
        if (chunk_size <= 0) {
            *result = JS_ThrowTypeError(ctx, "stream.read(size) expects a positive integer");
            return true;
        }
        buf = heap_caps_malloc((size_t)chunk_size + 1, MALLOC_CAP_8BIT);
        if (buf == NULL) {
            *result = JS_ThrowOutOfMemory(ctx);
            return true;
        }
        if (stream_slot_read(slot, buf, (size_t)chunk_size, &read_len) != 0) {
            heap_caps_free(buf);
            *result = JS_ThrowInternalError(ctx, "stream.read() failed");
            return true;
        }
        if (read_len == 0) {
            heap_caps_free(buf);
            *result = JS_NULL;
            return true;
        }
        buf[read_len] = '\0';
        *result = JS_NewStringLen(ctx, buf, read_len);
        heap_caps_free(buf);
        return true;
    }

    if (strcmp(operation, "write") == 0) {
        JSCStringBuf text_buf;
        const char *text;
        size_t text_len = 0;
        size_t written;

        if (!slot->writable || slot->kind != ESP32_MQUICKJS_STREAM_KIND_FILE) {
            *result = JS_ThrowTypeError(ctx, "stream.write(text) requires a writable file stream");
            return true;
        }
        if (argc < 2 || !JS_IsString(ctx, argv[1])) {
            *result = JS_ThrowTypeError(ctx, "stream.write(text) expects a string");
            return true;
        }
        text = JS_ToCStringLen(ctx, &text_len, argv[1], &text_buf);
        if (text == NULL) {
            *result = JS_EXCEPTION;
            return true;
        }
        written = fwrite(text, 1, text_len, slot->handle.file);
        if (written != text_len) {
            *result = JS_ThrowInternalError(ctx, "stream.write() failed for %s",
                                            slot->path != NULL ? slot->path : "<stream>");
            return true;
        }
        *result = JS_NewInt64(ctx, (int64_t)written);
        return true;
    }

    return false;
}
