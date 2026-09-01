#include "esp32_mquickjs_stream.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_fs.h"
#include "esp32_mquickjs_fs_events.h"
#include "esp32_mquickjs_future.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
    bool externally_acquired;
    bool close_queued;
    bool release_pending;
    bool write_dirty;
    uint16_t owner_count;
    uint16_t future_reservations;
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

typedef struct {
    int stream_id;
    uint32_t generation;
} esp32_mquickjs_stream_object_ref_t;

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

static bool stream_slot_busy(const esp32_mquickjs_stream_slot_t *slot)
{
    return slot != NULL &&
           (slot->externally_acquired || slot->future_reservations > 0);
}

static void stream_cleanup_slot(esp32_mquickjs_stream_slot_t *slot)
{
    if (slot == NULL) {
        return;
    }

    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_FILE && slot->handle.file != NULL) {
        int close_result = fclose(slot->handle.file);

        slot->handle.file = NULL;
        if (close_result == 0 && slot->write_dirty && slot->path != NULL) {
            esp32_mquickjs_fs_notify_change(
                ESP32_MQUICKJS_FS_CHANGE_WRITE, slot->path, NULL);
        }
    } else if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_MEMORY && slot->handle.memory.owned) {
        esp32_mquickjs_memory_payload_free(slot->handle.memory.data);
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
    slot->externally_acquired = false;
    slot->close_queued = false;
    slot->release_pending = false;
    slot->write_dirty = false;
    slot->owner_count = 0;
    slot->future_reservations = 0;
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
    esp32_mquickjs_stream_object_ref_t *ref;

    if (out_ref == NULL) {
        return -1;
    }
    if (JS_GetClassID(ctx, value) != JS_CLASS_STREAM ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "%s expects a Stream object", api_name);
        return -1;
    }
    out_ref->stream_id = ref->stream_id;
    out_ref->generation = ref->generation;
    return 0;
}

static int stream_ref_try_from_object(JSContext *ctx,
                                      JSValue value,
                                      esp32_mquickjs_fs_stream_ref_t *out_ref)
{
    esp32_mquickjs_stream_object_ref_t *ref;

    if (out_ref == NULL || JS_GetClassID(ctx, value) != JS_CLASS_STREAM ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        return -1;
    }
    out_ref->stream_id = ref->stream_id;
    out_ref->generation = ref->generation;
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
                                  esp32_mquickjs_stream_slot_t *slot)
{
    JSGCRef stream_ref;
    JSValue *stream_obj;
    esp32_mquickjs_stream_object_ref_t *object_ref;

    (void)global_obj;
    stream_obj = JS_PushGCRef(ctx, &stream_ref);
    *stream_obj = JS_NewObjectClassUser(ctx, JS_CLASS_STREAM);
    if (JS_IsException(*stream_obj)) {
        goto fail;
    }
    object_ref = heap_caps_malloc(sizeof(*object_ref), MALLOC_CAP_8BIT);
    if (object_ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    object_ref->stream_id = slot->stream_id;
    object_ref->generation = slot->generation;
    JS_SetOpaque(ctx, *stream_obj, object_ref);

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
        JS_SetOpaque(ctx, *stream_obj, NULL);
        heap_caps_free(object_ref);
        goto fail;
    }
    slot->owner_count++;
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
    FILE *file;
    bool path_existed;

    if (mode == NULL || strlen(mode) >= sizeof(s_streams[0].mode)) {
        return JS_ThrowTypeError(ctx, "unsupported stream mode");
    }
    path_existed = access(path, F_OK) == 0;
    file = fopen(path, mode);
    if (file == NULL) {
        return JS_ThrowInternalError(ctx, "open() failed for %s", path);
    }
    return esp32_mquickjs_stream_adopt_file(
        ctx, global_obj, path, mode, file,
        mode[0] == 'w' || (mode[0] == 'a' && !path_existed));
}

JSValue esp32_mquickjs_stream_adopt_file(JSContext *ctx,
                                         JSValue global_obj,
                                         const char *path,
                                         const char *mode,
                                         void *file_handle,
                                         bool initial_write_dirty)
{
    esp32_mquickjs_stream_slot_t *slot;
    JSValue result;

    if (path == NULL || mode == NULL || file_handle == NULL ||
        strlen(mode) >= sizeof(s_streams[0].mode)) {
        if (file_handle != NULL) {
            fclose((FILE *)file_handle);
        }
        return JS_ThrowTypeError(ctx, "invalid file stream");
    }
    slot = stream_alloc_slot();
    if (slot == NULL) {
        fclose((FILE *)file_handle);
        return JS_ThrowInternalError(ctx, "too many open streams");
    }
    if (!stream_parse_mode(mode, &slot->readable, &slot->writable)) {
        slot->allocated = false;
        fclose((FILE *)file_handle);
        return JS_ThrowTypeError(ctx, "unsupported stream mode: %s", mode);
    }
    slot->kind = ESP32_MQUICKJS_STREAM_KIND_FILE;
    slot->binary = strchr(mode, 'b') != NULL;
    slot->seekable = true;
    slot->write_dirty = initial_write_dirty;
    slot->handle.file = (FILE *)file_handle;
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
        esp32_mquickjs_memory_payload_free(data);
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
        esp32_mquickjs_memory_payload_free(data);
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
        copy = esp32_mquickjs_memory_payload_alloc(
            "stream.copy", source.length,
            ESP32_MQUICKJS_MEMORY_EXTERNAL);
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
    if (slot == NULL || !slot->readable || stream_slot_busy(slot)) {
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
        if (read_len > SIZE_MAX - length - 1U) {
            JS_ThrowOutOfMemory(ctx);
            goto done;
        }
        if (capacity < length + read_len + 1) {
            size_t new_capacity = capacity == 0 ? 1024 : capacity;
            char *grown;

            while (new_capacity < length + read_len + 1) {
                if (new_capacity > SIZE_MAX / 2U) {
                    new_capacity = length + read_len + 1U;
                    break;
                }
                new_capacity *= 2U;
            }
            grown = esp32_mquickjs_memory_payload_realloc(
                "stream.read", buffer, new_capacity,
                ESP32_MQUICKJS_MEMORY_EXTERNAL);
            if (grown == NULL) {
                JS_ThrowOutOfMemory(ctx);
                goto done;
            }
            buffer = grown;
            capacity = new_capacity;
        }
        memcpy(buffer + length, chunk, read_len);
        length += read_len;
    }

    if (buffer == NULL) {
        buffer = esp32_mquickjs_memory_payload_alloc(
            "stream.read", 1, ESP32_MQUICKJS_MEMORY_EXTERNAL);
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
    esp32_mquickjs_memory_payload_free(buffer);
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
    if (slot == NULL || !slot->readable || stream_slot_busy(slot)) {
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
            grown = esp32_mquickjs_memory_payload_realloc(
                "stream.read", buffer, new_capacity,
                ESP32_MQUICKJS_MEMORY_EXTERNAL);
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
    esp32_mquickjs_memory_payload_free(buffer);
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
    esp32_mquickjs_stream_slot_t *slot;

    if (stream_ref_try_from_object(ctx, stream_value, &ref) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    slot = stream_get_slot(&ref);
    if (slot == NULL || stream_slot_busy(slot)) {
        return ESP_ERR_INVALID_STATE;
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
    if (slot == NULL || !slot->externally_acquired) {
        return ESP_ERR_INVALID_STATE;
    }
    if (stream_slot_read(slot, buf, buf_len, out_len) != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t esp32_mquickjs_fs_stream_acquire(
    const esp32_mquickjs_fs_stream_ref_t *ref)
{
    esp32_mquickjs_stream_slot_t *slot = stream_get_slot(ref);

    if (slot == NULL || stream_slot_busy(slot)) {
        return ESP_ERR_INVALID_STATE;
    }
    slot->externally_acquired = true;
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

typedef enum {
    STREAM_FUTURE_READ,
    STREAM_FUTURE_WRITE,
    STREAM_FUTURE_FLUSH,
    STREAM_FUTURE_CLOSE,
    STREAM_FUTURE_SEEK,
} stream_future_kind_t;

struct esp32_mquickjs_future_driver_state {
    stream_future_kind_t kind;
    JSContext *ctx;
    esp32_mquickjs_fs_stream_ref_t ref;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    uint8_t *data;
    size_t data_length;
    size_t io_length;
    int offset;
    int whence;
    int64_t position;
    int error_number;
    bool binary;
    bool closed;
    bool reservation_retained;
    esp32_mquickjs_byte_span_source_t span_source;
    esp32_mquickjs_byte_span_t span;
    JSGCRef source_error_ref;
    bool span_source_opened;
    bool source_error_retained;
    bool span_worker_active;
    esp32_mquickjs_resource_key_t resource_key;
    _Atomic bool span_worker_completed;
    _Atomic bool completed;
    _Atomic bool cancelled;
};

static void stream_future_release(
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_stream_slot_t *slot;

    if (state == NULL) {
        return;
    }
    esp32_mquickjs_memory_payload_free(state->data);
    if (state->span_source_opened) {
        esp32_mquickjs_byte_span_source_close(state->ctx,
                                               &state->span_source);
        state->span_source_opened = false;
    }
    if (state->source_error_retained) {
        JS_DeleteGCRef(state->ctx, &state->source_error_ref);
        state->source_error_retained = false;
    }
    slot = stream_get_slot(&state->ref);
    if (slot != NULL) {
        if (state->reservation_retained && slot->future_reservations > 0) {
            slot->future_reservations--;
            state->reservation_retained = false;
        }
        if (state->kind == STREAM_FUTURE_CLOSE && !state->closed) {
            slot->close_queued = false;
        }
        if (slot->future_reservations == 0 && !slot->externally_acquired &&
            (state->closed || slot->release_pending)) {
            stream_cleanup_slot(slot);
        }
    }
    heap_caps_free(state);
}

static bool stream_future_prepare_common(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    stream_future_kind_t kind,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_stream_slot_t *slot;

    if (out_state == NULL) {
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    if (stream_ref_from_object(ctx, this_ref->val, "Stream operation",
                               &state->ref) != 0 ||
        (slot = stream_get_slot(&state->ref)) == NULL) {
        heap_caps_free(state);
        return false;
    }
    if (slot->externally_acquired || slot->close_queued ||
        slot->future_reservations == UINT16_MAX) {
        heap_caps_free(state);
        JS_ThrowInternalError(ctx,
                              "Stream operation refused because the stream is busy");
        return false;
    }
    atomic_init(&state->completed, false);
    atomic_init(&state->cancelled, false);
    atomic_init(&state->span_worker_completed, false);
    state->kind = kind;
    state->ctx = ctx;
    state->binary = slot->binary;
    esp32_mquickjs_byte_span_clear(&state->span);
    state->resource_key = slot->kind == ESP32_MQUICKJS_STREAM_KIND_FILE
        ? esp32_mquickjs_fs_resource_key_for_path(slot->path)
        : slot;
    if (state->resource_key == NULL) {
        JS_ThrowInternalError(ctx,
                              "Stream operation could not resolve its resource lane");
        goto fail;
    }
    slot->future_reservations++;
    state->reservation_retained = true;
    if (kind == STREAM_FUTURE_CLOSE) {
        slot->close_queued = true;
    }

    if (kind == STREAM_FUTURE_READ) {
        int chunk_size = ESP32_MQUICKJS_STREAM_READ_CHUNK_DEFAULT;

        if (!slot->readable || argc > 1 ||
            (argc == 1 && !JS_IsUndefined(argv[0].val) &&
             !JS_IsNull(argv[0].val) &&
             JS_ToInt32(ctx, &chunk_size, argv[0].val) != 0) ||
            chunk_size <= 0) {
            JS_ThrowTypeError(ctx,
                              "stream.read(size?) requires a readable stream and a positive size");
            goto fail;
        }
        state->data_length = (size_t)chunk_size;
        state->data = esp32_mquickjs_memory_payload_alloc(
            "stream.read",
            state->data_length + (state->binary ? 0U : 1U),
            ESP32_MQUICKJS_MEMORY_EXTERNAL);
        if (state->data == NULL) {
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }
    } else if (kind == STREAM_FUTURE_WRITE) {
        if (!slot->writable || slot->kind != ESP32_MQUICKJS_STREAM_KIND_FILE ||
            argc != 1) {
            JS_ThrowTypeError(ctx,
                              "stream.write(data) requires a writable file stream");
            goto fail;
        }
        if (slot->binary) {
            int class_id = JS_GetClassID(ctx, argv[0].val);

            if (class_id == JS_CLASS_BYTE_SPAN_SOURCE ||
                class_id == JS_CLASS_BITMAP_SPAN_SOURCE) {
                JSValue error = JS_UNDEFINED;

                if (!esp32_mquickjs_open_byte_span_source(
                        ctx, argv[0].val, "stream.write(data)",
                        &state->span_source, &error)) {
                    if (!JS_HasException(ctx)) {
                        JS_ThrowInternalError(
                            ctx,
                            "stream.write(data) could not open its source");
                    }
                    goto fail;
                }
                state->span_source_opened = true;
            } else {
                esp32_mquickjs_byte_source_t source;
                uint8_t *owned = NULL;
                JSValue error = JS_UNDEFINED;

                if (!esp32_mquickjs_get_byte_source(
                        ctx, argv[0].val, "stream.write(data)",
                        &source, &owned, &error)) {
                    goto fail;
                }
                state->data_length = source.length;
                if (source.length > 0) {
                    state->data = esp32_mquickjs_memory_payload_alloc(
                        "stream.write", source.length,
                        ESP32_MQUICKJS_MEMORY_EXTERNAL);
                    if (state->data != NULL) {
                        memcpy(state->data, source.data, source.length);
                    }
                }
                esp32_mquickjs_release_byte_source(owned);
                if (source.length > 0 && state->data == NULL) {
                    JS_ThrowOutOfMemory(ctx);
                    goto fail;
                }
            }
        } else {
            JSCStringBuf text_buf;
            const char *text;

            if (!JS_IsString(ctx, argv[0].val)) {
                JS_ThrowTypeError(
                    ctx, "stream.write(text) expects a string in text mode");
                goto fail;
            }
            text = JS_ToCStringLen(ctx, &state->data_length,
                                   argv[0].val, &text_buf);
            if (text == NULL) {
                goto fail;
            }
            if (state->data_length > 0) {
                state->data = esp32_mquickjs_memory_payload_alloc(
                    "stream.write", state->data_length,
                    ESP32_MQUICKJS_MEMORY_EXTERNAL);
                if (state->data == NULL) {
                    JS_ThrowOutOfMemory(ctx);
                    goto fail;
                }
                memcpy(state->data, text, state->data_length);
            }
        }
    } else if (kind == STREAM_FUTURE_FLUSH) {
        if (!slot->writable || argc != 0) {
            JS_ThrowTypeError(ctx,
                              "stream.flush() requires a writable stream");
            goto fail;
        }
    } else if (kind == STREAM_FUTURE_CLOSE) {
        if (argc != 0) {
            JS_ThrowTypeError(ctx, "stream.close() expects no arguments");
            goto fail;
        }
    } else {
        state->whence = SEEK_SET;
        if (!slot->seekable || argc < 1 || argc > 2 ||
            JS_ToInt32(ctx, &state->offset, argv[0].val) != 0 ||
            (argc == 2 && !JS_IsUndefined(argv[1].val) &&
             !JS_IsNull(argv[1].val) &&
             JS_ToInt32(ctx, &state->whence, argv[1].val) != 0)) {
            JS_ThrowTypeError(
                ctx, "stream.seek(offset, whence?) requires a seekable stream");
            goto fail;
        }
    }
    *out_state = state;
    return true;

fail:
    stream_future_release(state);
    return false;
}

#define STREAM_PREPARE(name, kind_value) \
    static bool name(JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv, \
                     esp32_mquickjs_future_driver_state_t **out_state) \
    { \
        return stream_future_prepare_common(ctx, this_ref, argc, argv, \
                                            kind_value, out_state); \
    }

STREAM_PREPARE(stream_read_future_prepare, STREAM_FUTURE_READ)
STREAM_PREPARE(stream_write_future_prepare, STREAM_FUTURE_WRITE)
STREAM_PREPARE(stream_flush_future_prepare, STREAM_FUTURE_FLUSH)
STREAM_PREPARE(stream_close_future_prepare, STREAM_FUTURE_CLOSE)
STREAM_PREPARE(stream_seek_future_prepare, STREAM_FUTURE_SEEK)

#undef STREAM_PREPARE

static void stream_future_retain_source_exception(
    esp32_mquickjs_future_driver_state_t *state,
    const char *fallback)
{
    JSValue *error;

    if (state == NULL || state->source_error_retained) {
        return;
    }
    if (!JS_HasException(state->ctx)) {
        (void)JS_ThrowInternalError(state->ctx, "%s", fallback);
    }
    error = JS_AddGCRef(state->ctx, &state->source_error_ref);
    *error = JS_GetException(state->ctx);
    state->source_error_retained = true;
}

static bool stream_future_next_source_span(
    esp32_mquickjs_future_driver_state_t *state)
{
    unsigned empty_spans = 0;

    while (state != NULL && state->span_source_opened &&
           !atomic_load_explicit(&state->cancelled, memory_order_acquire)) {
        if (!esp32_mquickjs_byte_span_source_next(
                state->ctx, &state->span_source, &state->span)) {
            if (JS_HasException(state->ctx)) {
                stream_future_retain_source_exception(
                    state, "stream.write(data) source iteration failed");
            }
            atomic_store_explicit(&state->completed, true,
                                  memory_order_release);
            return false;
        }
        if (state->span.length == 0) {
            if (++empty_spans > 16U) {
                (void)JS_ThrowInternalError(
                    state->ctx,
                    "stream.write(data) source produced too many empty spans");
                stream_future_retain_source_exception(
                    state, "stream.write(data) source iteration failed");
                atomic_store_explicit(&state->completed, true,
                                      memory_order_release);
                return false;
            }
            continue;
        }
        if (state->span.data == NULL ||
            state->span.length > SIZE_MAX - state->io_length) {
            (void)JS_ThrowInternalError(
                state->ctx,
                "stream.write(data) source produced an invalid span");
            stream_future_retain_source_exception(
                state, "stream.write(data) source produced an invalid span");
            atomic_store_explicit(&state->completed, true,
                                  memory_order_release);
            return false;
        }
        return true;
    }
    atomic_store_explicit(&state->completed, true, memory_order_release);
    return false;
}

static void stream_future_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    esp32_mquickjs_stream_slot_t *slot = state == NULL
        ? NULL : stream_get_slot(&state->ref);

    errno = 0;
    if (slot == NULL) {
        if (state != NULL) {
            state->error_number = EBADF;
        }
    } else if (state->kind == STREAM_FUTURE_READ) {
        if (stream_slot_read(slot, state->data, state->data_length,
                             &state->io_length) != 0) {
            state->error_number = errno != 0 ? errno : EIO;
        }
    } else if (state->kind == STREAM_FUTURE_WRITE) {
        if (state->span_source_opened) {
            if (!atomic_load_explicit(&state->cancelled,
                                      memory_order_acquire)) {
                size_t written = fwrite(state->span.data, 1,
                                        state->span.length,
                                        slot->handle.file);

                if (written != state->span.length) {
                    state->error_number = errno != 0 ? errno : EIO;
                } else {
                    state->io_length += written;
                    if (written > 0) {
                        slot->write_dirty = true;
                    }
                }
            }
            atomic_store_explicit(&state->span_worker_completed, true,
                                  memory_order_release);
            return;
        }
        state->io_length = fwrite(state->data, 1, state->data_length,
                                  slot->handle.file);
        if (state->io_length != state->data_length) {
            state->error_number = errno != 0 ? errno : EIO;
        } else if (state->io_length > 0) {
            slot->write_dirty = true;
        }
    } else if (state->kind == STREAM_FUTURE_FLUSH) {
        if (fflush(slot->handle.file) != 0) {
            state->error_number = errno != 0 ? errno : EIO;
        } else if (slot->write_dirty) {
            esp32_mquickjs_fs_notify_change(
                ESP32_MQUICKJS_FS_CHANGE_WRITE, slot->path, NULL);
            slot->write_dirty = false;
        }
    } else if (state->kind == STREAM_FUTURE_CLOSE) {
        if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_FILE &&
            slot->handle.file != NULL) {
            if (fclose(slot->handle.file) != 0) {
                state->error_number = errno != 0 ? errno : EIO;
            } else if (slot->write_dirty) {
                esp32_mquickjs_fs_notify_change(
                    ESP32_MQUICKJS_FS_CHANGE_WRITE, slot->path, NULL);
                slot->write_dirty = false;
            }
            slot->handle.file = NULL;
        }
        state->closed = true;
    } else if (stream_slot_seek(slot, state->offset, state->whence,
                                &state->position) != 0) {
        state->error_number = errno != 0 ? errno : EIO;
    }
    if (state != NULL) {
        atomic_store_explicit(&state->completed, true, memory_order_release);
    }
}

static bool stream_future_start(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_stream_slot_t *slot = state == NULL
        ? NULL : stream_get_slot(&state->ref);

    if (slot == NULL) {
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    if (slot->kind == ESP32_MQUICKJS_STREAM_KIND_FILE) {
        if (state->kind == STREAM_FUTURE_WRITE &&
            state->span_source_opened) {
            if (!stream_future_next_source_span(state)) {
                (void)esp32_mquickjs_future_wake(runtime, token);
                return true;
            }
            atomic_store_explicit(&state->span_worker_completed, false,
                                  memory_order_release);
            state->span_worker_active = true;
            if (!esp32_mquickjs_future_submit_worker(
                    runtime, token, stream_future_worker, state)) {
                state->span_worker_active = false;
                JS_ThrowInternalError(
                    ctx, "stream Future worker queue is busy");
                return false;
            }
            return true;
        }
        if (!esp32_mquickjs_future_submit_worker(
                runtime, token, stream_future_worker, state)) {
            JS_ThrowInternalError(ctx, "stream Future worker queue is busy");
            return false;
        }
        return true;
    }

    if (state->kind == STREAM_FUTURE_READ) {
        if (stream_slot_read(slot, state->data, state->data_length,
                             &state->io_length) != 0) {
            state->error_number = EIO;
        }
    } else if (state->kind == STREAM_FUTURE_FLUSH) {
        /* Writable memory streams do not currently exist. */
    } else if (state->kind == STREAM_FUTURE_CLOSE) {
        stream_cleanup_slot(slot);
        state->closed = true;
    } else if (state->kind == STREAM_FUTURE_SEEK &&
               stream_slot_seek(slot, state->offset, state->whence,
                                &state->position) != 0) {
        state->error_number = EIO;
    } else if (state->kind == STREAM_FUTURE_WRITE) {
        state->error_number = EBADF;
    }
    atomic_store_explicit(&state->completed, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static esp32_mquickjs_future_poll_t stream_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state != NULL && state->span_source_opened) {
        if (atomic_load_explicit(&state->completed, memory_order_acquire)) {
            return ESP32_MQUICKJS_FUTURE_READY;
        }
        if (!state->span_worker_active ||
            !atomic_load_explicit(&state->span_worker_completed,
                                  memory_order_acquire)) {
            return ESP32_MQUICKJS_FUTURE_PENDING;
        }
        state->span_worker_active = false;
        if (state->error_number != 0 ||
            atomic_load_explicit(&state->cancelled, memory_order_acquire) ||
            !stream_future_next_source_span(state)) {
            atomic_store_explicit(&state->completed, true,
                                  memory_order_release);
            return ESP32_MQUICKJS_FUTURE_READY;
        }
        atomic_store_explicit(&state->span_worker_completed, false,
                              memory_order_release);
        state->span_worker_active = true;
        if (!esp32_mquickjs_future_submit_worker(
                state->runtime, state->token, stream_future_worker, state)) {
            state->span_worker_active = false;
            state->error_number = EBUSY;
            atomic_store_explicit(&state->completed, true,
                                  memory_order_release);
            return ESP32_MQUICKJS_FUTURE_READY;
        }
        return ESP32_MQUICKJS_FUTURE_PENDING;
    }
    return state != NULL && atomic_load_explicit(
                                &state->completed, memory_order_acquire)
        ? ESP32_MQUICKJS_FUTURE_READY
        : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue stream_future_finish(
    JSContext *ctx,
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || atomic_load_explicit(
                             &state->cancelled, memory_order_acquire)) {
        return JS_ThrowInternalError(ctx, "stream operation cancelled");
    }
    if (state->source_error_retained) {
        return JS_Throw(ctx, state->source_error_ref.val);
    }
    if (state->error_number != 0) {
        return JS_ThrowInternalError(ctx, "stream operation failed (%s)",
                                     strerror(state->error_number));
    }
    if (state->kind == STREAM_FUTURE_READ) {
        if (state->io_length == 0) {
            return JS_NULL;
        }
        if (state->binary) {
            JSValue result = esp32_mquickjs_new_owned_byte_view(
                ctx, state->data, state->io_length);

            if (!JS_IsException(result)) {
                state->data = NULL;
            }
            return result;
        }
        state->data[state->io_length] = '\0';
        return JS_NewStringLen(ctx, (const char *)state->data,
                               state->io_length);
    }
    if (state->kind == STREAM_FUTURE_WRITE) {
        return JS_NewInt64(ctx, (int64_t)state->io_length);
    }
    if (state->kind == STREAM_FUTURE_SEEK) {
        return JS_NewInt64(ctx, state->position);
    }
    return JS_TRUE;
}

static esp32_mquickjs_cancel_result_t stream_future_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || atomic_load_explicit(
                             &state->completed, memory_order_acquire) ||
        atomic_exchange_explicit(&state->cancelled, true,
                                 memory_order_acq_rel)) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    return ESP32_MQUICKJS_CANCEL_REQUESTED;
}

static void stream_future_destroy(
    esp32_mquickjs_future_driver_state_t *state)
{
    stream_future_release(state);
}

static esp32_mquickjs_resource_key_t stream_future_resource_key(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL ? state->resource_key : NULL;
}

#define STREAM_FUTURE_DRIVER(name, prepare_fn) \
    static const esp32_mquickjs_future_driver_t name = { \
        .capture = prepare_fn, \
        .start = stream_future_start, \
        .poll = stream_future_poll, \
        .finish = stream_future_finish, \
        .cancel = stream_future_cancel, \
        .destroy = stream_future_destroy, \
        .resource_key = stream_future_resource_key, \
    }

STREAM_FUTURE_DRIVER(s_stream_read_driver, stream_read_future_prepare);
STREAM_FUTURE_DRIVER(s_stream_write_driver, stream_write_future_prepare);
STREAM_FUTURE_DRIVER(s_stream_flush_driver, stream_flush_future_prepare);
STREAM_FUTURE_DRIVER(s_stream_close_driver, stream_close_future_prepare);
STREAM_FUTURE_DRIVER(s_stream_seek_driver, stream_seek_future_prepare);

#undef STREAM_FUTURE_DRIVER

bool esp32_mquickjs_init_stream_runtime(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime)
{
    static const char *names[] = { "read", "write", "flush", "close", "seek" };
    static const esp32_mquickjs_future_driver_t *drivers[] = {
        &s_stream_read_driver, &s_stream_write_driver, &s_stream_flush_driver,
        &s_stream_close_driver, &s_stream_seek_driver,
    };
    JSGCRef global_ref;
    JSGCRef constructor_ref;
    JSGCRef prototype_ref;
    JSValue *global_obj = JS_PushGCRef(ctx, &global_ref);
    JSValue *constructor = JS_PushGCRef(ctx, &constructor_ref);
    JSValue *prototype = JS_PushGCRef(ctx, &prototype_ref);
    size_t index;
    bool success = true;

    *global_obj = JS_GetGlobalObject(ctx);
    *constructor = JS_IsException(*global_obj)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global_obj, "Stream");
    *prototype = JS_IsException(*constructor)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *constructor, "prototype");
    for (index = 0; success && index < sizeof(names) / sizeof(names[0]); ++index) {
        JSGCRef method_ref;
        JSValue *method = JS_PushGCRef(ctx, &method_ref);

        *method = JS_IsException(*prototype)
            ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *prototype, names[index]);
        success = !JS_IsException(*method) &&
                  esp32_mquickjs_future_register_driver(
                      ctx, runtime, *method, drivers[index]);
        JS_PopGCRef(ctx, &method_ref);
    }
    JS_PopGCRef(ctx, &prototype_ref);
    JS_PopGCRef(ctx, &constructor_ref);
    JS_PopGCRef(ctx, &global_ref);
    if (!success && !JS_HasException(ctx)) {
        JS_ThrowInternalError(ctx, "failed to register Stream Future drivers");
    }
    return success;
}

JSValue js_stream_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "Stream cannot be constructed directly");
}

void js_stream_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_stream_object_ref_t *object_ref = opaque;
    esp32_mquickjs_fs_stream_ref_t ref;
    esp32_mquickjs_stream_slot_t *slot;

    (void)ctx;
    if (object_ref == NULL) {
        return;
    }
    ref.stream_id = object_ref->stream_id;
    ref.generation = object_ref->generation;
    slot = stream_get_slot(&ref);
    if (slot != NULL) {
        if (slot->owner_count > 0) {
            slot->owner_count--;
        }
        if (slot->owner_count == 0) {
            if (stream_slot_busy(slot)) {
                slot->release_pending = true;
            } else {
                stream_cleanup_slot(slot);
            }
        }
    }
    heap_caps_free(object_ref);
}

static JSValue stream_future_call_and_wait(JSContext *ctx,
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
        : esp32_mquickjs_future_call_and_wait(
              ctx, esp32_mquickjs_get_active_runtime(), *method,
              *rooted_receiver, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &receiver_ref);
    return result;
}

#define STREAM_DIRECT_WRAPPER(function_name, method_name) \
    JSValue function_name(JSContext *ctx, JSValue *this_val, \
                          int argc, JSValue *argv) \
    { \
        return stream_future_call_and_wait(ctx, *this_val, method_name, \
                                           argc, argv); \
    }

STREAM_DIRECT_WRAPPER(js_stream_close, "close")
STREAM_DIRECT_WRAPPER(js_stream_flush, "flush")

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
    if (stream_slot_busy(slot)) {
        return JS_ThrowInternalError(ctx, "stream.tell() refused because the stream is busy");
    }
    pos = stream_slot_tell(slot);
    if (pos < 0) {
        return JS_ThrowInternalError(ctx, "stream.tell() failed");
    }
    return JS_NewInt64(ctx, pos);
}

STREAM_DIRECT_WRAPPER(js_stream_seek, "seek")

JSValue js_stream_eof(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_fs_stream_ref_t stream_ref;
    esp32_mquickjs_stream_slot_t *slot = NULL;

    (void)argc;
    (void)argv;
    if (stream_get_this_slot(ctx, *this_val, "stream.eof", &stream_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (stream_slot_busy(slot)) {
        return JS_ThrowInternalError(ctx, "stream.eof() refused because the stream is busy");
    }
    return JS_NewBool(stream_slot_eof(slot));
}

STREAM_DIRECT_WRAPPER(js_stream_read, "read")
STREAM_DIRECT_WRAPPER(js_stream_write, "write")

#undef STREAM_DIRECT_WRAPPER
