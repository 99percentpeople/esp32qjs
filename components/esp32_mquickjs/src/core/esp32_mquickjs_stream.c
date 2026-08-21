#include "esp32_mquickjs_stream.h"
#include "esp32_mquickjs_core.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "utils/esp32_mquickjs_byte_source.h"

#define ESP32_MQUICKJS_MAX_STREAMS 16
#define ESP32_MQUICKJS_STREAM_READ_CHUNK_DEFAULT 1024
#define ESP32_MQUICKJS_STREAM_READ_ALL_CHUNK 1024

typedef enum {
    ESP32_MQUICKJS_STREAM_KIND_FILE = 0,
    ESP32_MQUICKJS_STREAM_KIND_MEMORY = 1,
    ESP32_MQUICKJS_STREAM_KIND_SOURCE = 2,
} esp32_mquickjs_stream_kind_t;

typedef struct {
    uint8_t stream_id;
    bool allocated;
    uint32_t generation;
    esp32_mquickjs_stream_kind_t kind;
    char mode[8];
    bool readable;
    bool writable;
    bool binary;
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
        struct {
            JSContext *ctx;
            JSGCRef owner_ref;
            esp32_mquickjs_byte_span_source_t source;
            esp32_mquickjs_byte_span_t span;
            size_t span_offset;
            size_t total_read;
            size_t known_length;
            bool rooted;
            bool eof;
            bool has_known_length;
        } source;
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
    return kind == ESP32_MQUICKJS_STREAM_KIND_MEMORY ? "memory" :
           kind == ESP32_MQUICKJS_STREAM_KIND_SOURCE ? "source" : "file";
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
    } else if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_SOURCE) {
        esp32_mquickjs_byte_span_source_close(slot->handle.source.ctx,
                                              &slot->handle.source.source);
        if (slot->handle.source.rooted) {
            JS_DeleteGCRef(slot->handle.source.ctx,
                           &slot->handle.source.owner_ref);
            slot->handle.source.rooted = false;
        }
    }

    heap_caps_free(slot->path);
    slot->path = NULL;
    slot->allocated = false;
    slot->readable = false;
    slot->writable = false;
    slot->binary = false;
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
    JSGCRef object_ref;
    JSGCRef id_ref;
    JSGCRef generation_ref;
    JSValue *object;
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

    object = JS_PushGCRef(ctx, &object_ref);
    id_value = JS_PushGCRef(ctx, &id_ref);
    generation_value = JS_PushGCRef(ctx, &generation_ref);
    *object = value;
    *id_value = JS_GetPropertyStr(ctx, *object, ESP32_MQUICKJS_FS_STREAM_ID_KEY);
    *generation_value = JS_GetPropertyStr(ctx, *object, ESP32_MQUICKJS_FS_STREAM_GENERATION_KEY);
    if (JS_IsException(*id_value) || JS_IsException(*generation_value)) {
        JS_PopGCRef(ctx, &generation_ref);
        JS_PopGCRef(ctx, &id_ref);
        JS_PopGCRef(ctx, &object_ref);
        return -1;
    }
    if (JS_ToInt32(ctx, &stream_id, *id_value) != 0 || JS_ToUint32(ctx, &generation, *generation_value) != 0) {
        JS_PopGCRef(ctx, &generation_ref);
        JS_PopGCRef(ctx, &id_ref);
        JS_PopGCRef(ctx, &object_ref);
        JS_ThrowTypeError(ctx, "%s expects a Stream object", api_name);
        return -1;
    }

    JS_PopGCRef(ctx, &generation_ref);
    JS_PopGCRef(ctx, &id_ref);
    JS_PopGCRef(ctx, &object_ref);
    out_ref->stream_id = stream_id;
    out_ref->generation = generation;
    return 0;
}

static int stream_ref_try_from_object(JSContext *ctx,
                                      JSValue value,
                                      esp32_mquickjs_fs_stream_ref_t *out_ref)
{
    JSGCRef object_ref;
    JSGCRef id_ref;
    JSGCRef generation_ref;
    JSValue *object;
    JSValue *id_value;
    JSValue *generation_value;
    int stream_id;
    uint32_t generation;

    if (out_ref == NULL || JS_IsUndefined(value) || JS_IsNull(value)) {
        return -1;
    }

    object = JS_PushGCRef(ctx, &object_ref);
    id_value = JS_PushGCRef(ctx, &id_ref);
    generation_value = JS_PushGCRef(ctx, &generation_ref);
    *object = value;
    *id_value = JS_GetPropertyStr(ctx, *object, ESP32_MQUICKJS_FS_STREAM_ID_KEY);
    *generation_value = JS_GetPropertyStr(ctx, *object, ESP32_MQUICKJS_FS_STREAM_GENERATION_KEY);
    if (JS_IsException(*id_value) || JS_IsException(*generation_value) ||
        JS_ToInt32(ctx, &stream_id, *id_value) != 0 ||
        JS_ToUint32(ctx, &generation, *generation_value) != 0) {
        JS_PopGCRef(ctx, &generation_ref);
        JS_PopGCRef(ctx, &id_ref);
        JS_PopGCRef(ctx, &object_ref);
        return -1;
    }

    JS_PopGCRef(ctx, &generation_ref);
    JS_PopGCRef(ctx, &id_ref);
    JS_PopGCRef(ctx, &object_ref);
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

static int stream_get_this_slot(JSContext *ctx,
                                JSValue this_value,
                                const char *api_name,
                                esp32_mquickjs_fs_stream_ref_t *out_ref,
                                esp32_mquickjs_stream_slot_t **out_slot)
{
    return stream_get_bound_ref(ctx, this_value, api_name, out_ref, out_slot);
}

static JSValue stream_make_object(JSContext *ctx,
                                  JSValue global_obj,
                                  const esp32_mquickjs_stream_slot_t *slot)
{
    JSGCRef stream_ref;
    JSValue *stream_obj;

    (void)global_obj;
    stream_obj = JS_PushGCRef(ctx, &stream_ref);
    *stream_obj = JS_NewObjectClassUser(ctx, JS_CLASS_STREAM);
    if (JS_IsException(*stream_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, stream_obj, "kind",
                                     JS_NewString(ctx, stream_kind_name(slot->kind))) ||
        !esp32_mquickjs_set_property_ref(ctx, stream_obj, "mode",
                                     JS_NewString(ctx, slot->mode[0] != '\0' ? slot->mode : "r")) ||
        !esp32_mquickjs_set_property_ref(ctx, stream_obj, "path",
                                     JS_NewString(ctx, slot->path != NULL ? slot->path : "")) ||
        !esp32_mquickjs_set_property_ref(ctx, stream_obj, "readable",
                                     JS_NewBool(slot->readable)) ||
        !esp32_mquickjs_set_property_ref(ctx, stream_obj, "writable",
                                     JS_NewBool(slot->writable)) ||
        !esp32_mquickjs_set_property_ref(ctx, stream_obj, "seekable",
                                     JS_NewBool(slot->seekable)) ||
        !esp32_mquickjs_set_property_ref(ctx, stream_obj, ESP32_MQUICKJS_FS_STREAM_ID_KEY,
                                     JS_NewInt32(ctx, slot->stream_id)) ||
        !esp32_mquickjs_set_property_ref(ctx, stream_obj, ESP32_MQUICKJS_FS_STREAM_GENERATION_KEY,
                                     JS_NewUint32(ctx, slot->generation))) {
        goto fail;
    }
    return JS_PopGCRef(ctx, &stream_ref);

fail:
    JS_PopGCRef(ctx, &stream_ref);
    return JS_EXCEPTION;
}

static int stream_open_memory_owned_slot(uint8_t *data,
                                         size_t data_len,
                                         bool binary,
                                         esp32_mquickjs_stream_slot_t **out_slot)
{
    esp32_mquickjs_stream_slot_t *slot = stream_alloc_slot();

    if (slot == NULL) {
        return -1;
    }

    slot->kind = ESP32_MQUICKJS_STREAM_KIND_MEMORY;
    slot->readable = true;
    slot->writable = false;
    slot->binary = binary;
    slot->seekable = true;
    memcpy(slot->mode, binary ? "rb" : "r", binary ? 3 : 2);
    slot->handle.memory.data = data;
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

    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_SOURCE) {
        size_t copied = 0;
        unsigned empty_spans = 0;

        while (copied < buf_len && !slot->handle.source.eof) {
            size_t available;
            size_t take;

            if (slot->handle.source.span_offset >= slot->handle.source.span.length) {
                esp32_mquickjs_byte_span_clear(&slot->handle.source.span);
                slot->handle.source.span_offset = 0;
                if (!esp32_mquickjs_byte_span_source_next(
                        slot->handle.source.ctx,
                        &slot->handle.source.source,
                        &slot->handle.source.span)) {
                    if (JS_HasException(slot->handle.source.ctx)) {
                        return -1;
                    }
                    slot->handle.source.eof = true;
                    break;
                }
                if (slot->handle.source.span.length == 0) {
                    if (++empty_spans > 16) {
                        return -1;
                    }
                    continue;
                }
                if (slot->handle.source.span.data == NULL) {
                    return -1;
                }
            }
            available = slot->handle.source.span.length -
                        slot->handle.source.span_offset;
            take = available < (buf_len - copied) ? available :
                   (buf_len - copied);
            memcpy((uint8_t *)buf + copied,
                   slot->handle.source.span.data +
                       slot->handle.source.span_offset,
                   take);
            slot->handle.source.span_offset += take;
            slot->handle.source.total_read += take;
            copied += take;
        }
        *out_len = copied;
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
    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_SOURCE) {
        return (int64_t)slot->handle.source.total_read;
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
    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_SOURCE) {
        return slot->handle.source.eof;
    }
    return true;
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
    slot->binary = strchr(mode, 'b') != NULL;
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
    if (stream_open_memory_owned_slot((uint8_t *)data, data_len, false, &slot) != 0) {
        heap_caps_free(data);
        return JS_ThrowInternalError(ctx, "too many open streams");
    }

    result = stream_make_object(ctx, global_obj, slot);
    if (JS_IsException(result)) {
        stream_cleanup_slot(slot);
    }
    return result;
}

JSValue esp32_mquickjs_stream_open_memory_owned_binary(JSContext *ctx,
                                                       JSValue global_obj,
                                                       uint8_t *data,
                                                       size_t data_len)
{
    esp32_mquickjs_stream_slot_t *slot;
    JSValue result;

    if (data == NULL && data_len != 0) {
        return JS_ThrowInternalError(ctx, "binary memory stream requires valid data");
    }
    if (stream_open_memory_owned_slot(data, data_len, true, &slot) != 0) {
        heap_caps_free(data);
        return JS_ThrowInternalError(ctx, "too many open streams");
    }
    result = stream_make_object(ctx, global_obj, slot);
    if (JS_IsException(result)) {
        stream_cleanup_slot(slot);
    }
    return result;
}

JSValue esp32_mquickjs_stream_open_bytes_copy(JSContext *ctx,
                                              JSValue global_obj,
                                              JSValue bytes_value,
                                              const char *api_name)
{
    esp32_mquickjs_byte_source_t source;
    uint8_t *converted = NULL;
    uint8_t *copy = NULL;
    JSValue error = JS_UNDEFINED;

    if (!esp32_mquickjs_get_byte_source(ctx, bytes_value, api_name,
                                        &source, &converted, &error)) {
        return JS_IsUndefined(error) ? JS_EXCEPTION : error;
    }
    if (source.length > 0) {
        copy = heap_caps_malloc(source.length, MALLOC_CAP_8BIT);
        if (copy == NULL) {
            esp32_mquickjs_release_byte_source(converted);
            return JS_ThrowOutOfMemory(ctx);
        }
        memcpy(copy, source.data, source.length);
    }
    esp32_mquickjs_release_byte_source(converted);
    return esp32_mquickjs_stream_open_memory_owned_binary(ctx, global_obj,
                                                          copy, source.length);
}

JSValue esp32_mquickjs_stream_open_byte_source(JSContext *ctx,
                                               JSValue global_obj,
                                               JSValue source_value)
{
    esp32_mquickjs_stream_slot_t *slot = stream_alloc_slot();
    JSValue error = JS_UNDEFINED;
    JSValue *owner;
    JSValue result;

    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "too many open streams");
    }
    slot->kind = ESP32_MQUICKJS_STREAM_KIND_SOURCE;
    slot->readable = true;
    slot->binary = true;
    slot->seekable = false;
    memcpy(slot->mode, "rb", 3);
    slot->handle.source.ctx = ctx;
    esp32_mquickjs_byte_span_clear(&slot->handle.source.span);
    if (!esp32_mquickjs_open_byte_span_source(ctx, source_value,
                                              "binary body",
                                              &slot->handle.source.source,
                                              &error)) {
        slot->allocated = false;
        return JS_IsUndefined(error) ? JS_EXCEPTION : error;
    }
    owner = JS_AddGCRef(ctx, &slot->handle.source.owner_ref);
    *owner = source_value;
    slot->handle.source.rooted = true;
    slot->handle.source.has_known_length =
        esp32_mquickjs_byte_span_source_known_length(
            ctx, source_value, &slot->handle.source.known_length);
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

int esp32_mquickjs_stream_read_all_bytes(JSContext *ctx,
                                         JSValue stream_value,
                                         const char *api_name,
                                         size_t max_bytes,
                                         uint8_t **out_data,
                                         size_t *out_len)
{
    esp32_mquickjs_fs_stream_ref_t ref;
    esp32_mquickjs_stream_slot_t *slot;
    uint8_t *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;

    if (out_data == NULL || out_len == NULL || max_bytes == 0) {
        return -1;
    }
    *out_data = NULL;
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
        size_t needed;

        if (stream_slot_read(slot, chunk, sizeof(chunk), &read_len) != 0) {
            JS_ThrowInternalError(ctx, "%s failed while reading the stream", api_name);
            goto fail;
        }
        if (read_len == 0) {
            break;
        }
        if (read_len > max_bytes || length > max_bytes - read_len) {
            JS_ThrowRangeError(ctx, "%s body exceeds maxBytes", api_name);
            goto fail;
        }
        needed = length + read_len;
        if (capacity < needed) {
            size_t new_capacity = capacity == 0 ? 1024 : capacity;
            uint8_t *grown;

            while (new_capacity < needed && new_capacity < max_bytes) {
                size_t doubled = new_capacity > SIZE_MAX / 2
                                     ? max_bytes
                                     : new_capacity * 2;
                new_capacity = doubled > max_bytes ? max_bytes : doubled;
            }
            grown = heap_caps_realloc(buffer, new_capacity,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (grown == NULL) {
                grown = heap_caps_realloc(buffer, new_capacity,
                                          MALLOC_CAP_8BIT);
            }
            if (grown == NULL) {
                JS_ThrowOutOfMemory(ctx);
                goto fail;
            }
            buffer = grown;
            capacity = new_capacity;
        }
        memcpy(buffer + length, chunk, read_len);
        length += read_len;
    }

    *out_data = buffer;
    *out_len = length;
    return 0;

fail:
    heap_caps_free(buffer);
    return -1;
}

bool esp32_mquickjs_stream_is_binary(
    const esp32_mquickjs_fs_stream_ref_t *ref)
{
    esp32_mquickjs_stream_slot_t *slot = stream_get_slot(ref);

    return slot != NULL && slot->binary;
}

bool esp32_mquickjs_stream_known_length(
    const esp32_mquickjs_fs_stream_ref_t *ref,
    size_t *out_length)
{
    esp32_mquickjs_stream_slot_t *slot = stream_get_slot(ref);

    if (slot == NULL || out_length == NULL) {
        return false;
    }
    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_MEMORY) {
        *out_length = slot->handle.memory.len - slot->handle.memory.pos;
        return true;
    }
    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_SOURCE &&
        slot->handle.source.has_known_length &&
        slot->handle.source.total_read <= slot->handle.source.known_length) {
        *out_length = slot->handle.source.known_length -
                      slot->handle.source.total_read;
        return true;
    }
    return false;
}

void esp32_mquickjs_deinit_stream_runtime(void)
{
    size_t i;

    stream_init_once();
    for (i = 0; i < ESP32_MQUICKJS_MAX_STREAMS; ++i) {
        if (s_streams[i].allocated) {
            stream_cleanup_slot(&s_streams[i]);
        }
    }
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

JSValue js_stream_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "Stream cannot be constructed directly");
}

JSValue js_stream_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_fs_stream_ref_t stream_ref;
    esp32_mquickjs_stream_slot_t *slot = NULL;

    (void)argc;
    (void)argv;
    if (stream_get_this_slot(ctx, *this_val, "stream.close", &stream_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    stream_cleanup_slot(slot);
    return JS_TRUE;
}

JSValue js_stream_flush(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_fs_stream_ref_t stream_ref;
    esp32_mquickjs_stream_slot_t *slot = NULL;

    (void)argc;
    (void)argv;
    if (stream_get_this_slot(ctx, *this_val, "stream.flush", &stream_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (!slot->writable) {
        return JS_ThrowTypeError(ctx, "stream.flush() requires a writable stream");
    }
    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_MEMORY) {
        return JS_TRUE;
    }
    if (fflush(slot->handle.file) != 0) {
        return JS_ThrowInternalError(ctx, "stream.flush() failed for %s",
                                     slot->path != NULL ? slot->path : "<stream>");
    }
    return JS_TRUE;
}

JSValue js_stream_tell(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_fs_stream_ref_t stream_ref;
    esp32_mquickjs_stream_slot_t *slot = NULL;
    int64_t pos;

    (void)argc;
    (void)argv;
    if (stream_get_this_slot(ctx, *this_val, "stream.tell", &stream_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    pos = stream_slot_tell(slot);
    if (pos < 0) {
        return JS_ThrowInternalError(ctx, "stream.tell() failed");
    }
    return JS_NewInt64(ctx, pos);
}

JSValue js_stream_seek(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_fs_stream_ref_t stream_ref;
    esp32_mquickjs_stream_slot_t *slot = NULL;
    int offset = 0;
    int whence = SEEK_SET;
    int64_t pos = 0;

    if (stream_get_this_slot(ctx, *this_val, "stream.seek", &stream_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (argc < 1 || JS_ToInt32(ctx, &offset, argv[0]) != 0) {
        return JS_ThrowTypeError(ctx, "stream.seek(offset, whence?) expects an integer offset");
    }
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]) &&
        JS_ToInt32(ctx, &whence, argv[1]) != 0) {
        return JS_ThrowTypeError(ctx, "stream.seek(offset, whence) expects a valid whence");
    }
    if (stream_slot_seek(slot, offset, whence, &pos) != 0) {
        return JS_ThrowInternalError(ctx, "stream.seek() failed");
    }
    return JS_NewInt64(ctx, pos);
}

JSValue js_stream_eof(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_fs_stream_ref_t stream_ref;
    esp32_mquickjs_stream_slot_t *slot = NULL;

    (void)argc;
    (void)argv;
    if (stream_get_this_slot(ctx, *this_val, "stream.eof", &stream_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    return JS_NewBool(stream_slot_eof(slot));
}

JSValue js_stream_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_fs_stream_ref_t stream_ref;
    esp32_mquickjs_stream_slot_t *slot = NULL;
    int chunk_size = ESP32_MQUICKJS_STREAM_READ_CHUNK_DEFAULT;
    uint8_t *buf;
    size_t read_len = 0;

    if (stream_get_this_slot(ctx, *this_val, "stream.read", &stream_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (!slot->readable) {
        return JS_ThrowTypeError(ctx, "stream.read(size?) requires a readable stream");
    }
    if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]) &&
        JS_ToInt32(ctx, &chunk_size, argv[0]) != 0) {
        return JS_ThrowTypeError(ctx, "stream.read(size) expects a positive integer");
    }
    if (chunk_size <= 0) {
        return JS_ThrowTypeError(ctx, "stream.read(size) expects a positive integer");
    }
    buf = heap_caps_malloc((size_t)chunk_size + (slot->binary ? 0 : 1), MALLOC_CAP_8BIT);
    if (buf == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    if (stream_slot_read(slot, buf, (size_t)chunk_size, &read_len) != 0) {
        heap_caps_free(buf);
        return JS_ThrowInternalError(ctx, "stream.read() failed");
    }
    if (read_len == 0) {
        heap_caps_free(buf);
        return JS_NULL;
    }
    if (slot->binary) {
        return esp32_mquickjs_new_owned_byte_view(ctx, buf, read_len);
    }
    buf[read_len] = '\0';
    {
        JSValue result = JS_NewStringLen(ctx, (const char *)buf, read_len);
        heap_caps_free(buf);
        return result;
    }
}

JSValue js_stream_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_fs_stream_ref_t stream_ref;
    esp32_mquickjs_stream_slot_t *slot = NULL;
    JSCStringBuf text_buf;
    const char *text;
    size_t text_len = 0;
    size_t written;

    if (stream_get_this_slot(ctx, *this_val, "stream.write", &stream_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (!slot->writable || slot->kind != ESP32_MQUICKJS_STREAM_KIND_FILE) {
        return JS_ThrowTypeError(ctx, "stream.write(text) requires a writable file stream");
    }
    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "stream.write(text) expects a string");
    }
    text = JS_ToCStringLen(ctx, &text_len, argv[0], &text_buf);
    if (text == NULL) {
        return JS_EXCEPTION;
    }
    written = fwrite(text, 1, text_len, slot->handle.file);
    if (written != text_len) {
        return JS_ThrowInternalError(ctx, "stream.write() failed for %s",
                                     slot->path != NULL ? slot->path : "<stream>");
    }
    return JS_NewInt64(ctx, (int64_t)written);
}
