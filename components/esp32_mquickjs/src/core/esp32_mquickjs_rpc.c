#include "esp32_mquickjs_rpc.h"

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_fs_events.h"
#include "esp32_mquickjs_options.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include "esp32qjs_rpc_wire.h"

#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cbor.h"
#include "esp_timer.h"

#define RPC_DECODER_SLOTS 4U
#define RPC_CODEC_SLOTS 2U
#define RPC_CODEC_FIELDS 256U
#define RPC_CODEC_FIELD_BYTES 64U
#define RPC_CODEC_TOTAL_FIELD_BYTES 8192U
#define RPC_CBOR_DEPTH 16U
#define RPC_SAFE_INTEGER 9007199254740991.0
#define RPC_STREAM_DIRECTORY_BYTES 96U
#define RPC_STREAM_PATH_BYTES 160U
#define RPC_FILE_SOURCE_CHUNK_BYTES 4096U

typedef struct rpc_file_source_object rpc_file_source_object_t;
typedef struct rpc_codec_slot rpc_codec_slot_t;

typedef struct {
    uint8_t slot;
    uint32_t generation;
} rpc_handle_ref_t;

typedef struct {
    rpc_file_source_object_t *owner;
    FILE *file;
    uint8_t buffer[RPC_FILE_SOURCE_CHUNK_BYTES];
} rpc_file_source_iterator_t;

typedef struct {
    JSContext *ctx;
    JSGCRef source_ref;
    bool rooted;
    bool opened;
    bool consumed;
    bool destroy_requested;
    uint16_t opcode;
    uint32_t request_id;
    uint8_t flags;
    uint8_t *prefix;
    size_t prefix_len;
    size_t source_len;
    size_t logical_offset;
    size_t source_produced;
    esp32_mquickjs_byte_span_source_t source;
    esp32_mquickjs_byte_span_t span;
    size_t span_offset;
    uint8_t segment[ESP32_MQUICKJS_RPC_SEGMENT_BYTES];
    uint8_t frame[ESP32_MQUICKJS_RPC_MAX_FRAME_BYTES];
} rpc_encoded_stream_object_t;

struct rpc_file_source_object {
    char *path;
    size_t length;
    uint32_t crc32;
    bool has_crc32;
    bool remove_on_destroy;
    bool adopted;
    bool opened;
    bool consumed;
    bool destroy_requested;
};

typedef struct {
    bool used;
    uint32_t generation;
    rpc_codec_slot_t *codec;
    esp32_mquickjs_rpc_wire_decoder_t wire;
    FILE *stream_file;
    char stream_path[RPC_STREAM_PATH_BYTES];
    size_t stream_received;
    size_t stream_expected;
    uint32_t stream_crc_state;
    uint32_t messages;
    uint32_t errors;
} rpc_decoder_slot_t;

struct rpc_codec_slot {
    bool used;
    bool close_pending;
    uint32_t generation;
    bool allow_string_keys;
    char **fields;
    bool *dynamic_fields;
    size_t field_count;
    char stream_directory[RPC_STREAM_DIRECTORY_BYTES];
    uint32_t decoders;
};

typedef struct {
    uint8_t *data;
    size_t length;
    size_t capacity;
    bool failed;
    bool sealed;
    bool has_stream;
    size_t stream_length;
    JSValue stream_value;
} rpc_buffer_t;

typedef struct {
    char *name;
    uint32_t field_id;
    bool integer_key;
    uint8_t encoded_key[9];
    size_t encoded_key_len;
} rpc_map_key_t;

typedef struct {
    JSContext *ctx;
    JSValue *messages;
    rpc_decoder_slot_t *slot;
    uint32_t message_index;
    bool failed;
} rpc_feed_context_t;

static rpc_decoder_slot_t s_rpc_decoders[RPC_DECODER_SLOTS];
static rpc_codec_slot_t s_rpc_codecs[RPC_CODEC_SLOTS];

static bool rpc_file_source_next(JSContext *ctx,
                                 void *opaque,
                                 esp32_mquickjs_byte_span_t *out)
{
    rpc_file_source_iterator_t *iterator = opaque;
    size_t length;

    if (iterator == NULL || iterator->file == NULL || iterator->owner == NULL) {
        JS_ThrowReferenceError(ctx, "RPC file source iterator is closed");
        return false;
    }
    length = fread(iterator->buffer, 1, sizeof(iterator->buffer), iterator->file);
    if (length == 0) {
        if (ferror(iterator->file)) {
            JS_ThrowInternalError(ctx, "RPC file source read failed");
        }
        return false;
    }
    out->data = iterator->buffer;
    out->length = length;
    out->owner = JS_UNDEFINED;
    out->dma_capable = false;
    return true;
}

static void rpc_file_source_iterator_close(JSContext *ctx, void *opaque)
{
    rpc_file_source_iterator_t *iterator = opaque;
    rpc_file_source_object_t *owner;

    (void)ctx;
    if (iterator == NULL) {
        return;
    }
    owner = iterator->owner;
    if (iterator->file != NULL) {
        fclose(iterator->file);
    }
    if (owner != NULL) {
        owner->opened = false;
        owner->consumed = true;
        if (owner->destroy_requested) {
            if (owner->remove_on_destroy && !owner->adopted && owner->path != NULL) {
                unlink(owner->path);
            }
            free(owner->path);
            free(owner);
        }
    }
    free(iterator);
}

static bool rpc_file_source_open(JSContext *ctx,
                                 JSValue source_value,
                                 void *opaque,
                                 esp32_mquickjs_byte_span_source_t *out,
                                 JSValue *out_error)
{
    rpc_file_source_object_t *source = opaque;
    rpc_file_source_iterator_t *iterator;

    (void)source_value;
    if (source == NULL || source->path == NULL || source->opened ||
        source->consumed || source->destroy_requested || source->adopted) {
        *out_error = JS_ThrowReferenceError(
            ctx, "RPC file ByteSpanSource is closed or has already been consumed");
        return false;
    }
    iterator = calloc(1, sizeof(*iterator));
    if (iterator == NULL) {
        *out_error = JS_ThrowOutOfMemory(ctx);
        return false;
    }
    iterator->file = fopen(source->path, "rb");
    if (iterator->file == NULL) {
        free(iterator);
        *out_error = JS_ThrowInternalError(ctx, "RPC file source could not be opened");
        return false;
    }
    iterator->owner = source;
    source->opened = true;
    out->opaque = iterator;
    out->next = rpc_file_source_next;
    out->close = rpc_file_source_iterator_close;
    return true;
}

static size_t rpc_file_source_known_length(void *opaque)
{
    rpc_file_source_object_t *source = opaque;
    return source == NULL || source->adopted ? 0 : source->length;
}

static void rpc_file_source_destroy(JSContext *ctx, void *opaque)
{
    rpc_file_source_object_t *source = opaque;

    (void)ctx;
    if (source == NULL) {
        return;
    }
    if (source->opened) {
        source->destroy_requested = true;
        return;
    }
    if (source->remove_on_destroy && !source->adopted && source->path != NULL) {
        unlink(source->path);
    }
    free(source->path);
    free(source);
}

static const esp32_mquickjs_byte_span_source_object_ops_t s_rpc_file_source_ops = {
    .class_id = JS_CLASS_BYTE_SPAN_SOURCE,
    .open = rpc_file_source_open,
    .known_length = rpc_file_source_known_length,
    .destroy = rpc_file_source_destroy,
};

static JSValue rpc_new_file_source(JSContext *ctx,
                                   const char *path,
                                   size_t length,
                                   bool remove_on_destroy,
                                   bool has_crc32,
                                   uint32_t crc32)
{
    rpc_file_source_object_t *source;
    size_t path_len;

    if (path == NULL) {
        return JS_ThrowInternalError(ctx, "RPC file source path is missing");
    }
    source = calloc(1, sizeof(*source));
    if (source == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    path_len = strlen(path);
    source->path = malloc(path_len + 1U);
    if (source->path == NULL) {
        free(source);
        return JS_ThrowOutOfMemory(ctx);
    }
    memcpy(source->path, path, path_len + 1U);
    source->length = length;
    source->remove_on_destroy = remove_on_destroy;
    source->has_crc32 = has_crc32;
    source->crc32 = crc32;
    return esp32_mquickjs_new_byte_span_source(
        ctx, JS_UNDEFINED, &s_rpc_file_source_ops, source);
}

static void rpc_decoder_stream_cleanup(rpc_decoder_slot_t *slot)
{
    if (slot == NULL) {
        return;
    }
    if (slot->stream_file != NULL) {
        fclose(slot->stream_file);
        slot->stream_file = NULL;
    }
    if (slot->stream_path[0] != '\0') {
        unlink(slot->stream_path);
        slot->stream_path[0] = '\0';
    }
    slot->stream_received = 0;
    slot->stream_expected = 0;
    slot->stream_crc_state = UINT32_C(0xffffffff);
}

static bool rpc_buffer_append(rpc_buffer_t *buffer, const void *data, size_t length)
{
    if (buffer->failed || buffer->sealed ||
        length > buffer->capacity - buffer->length) {
        buffer->failed = true;
        return false;
    }
    if (length != 0) {
        memcpy(buffer->data + buffer->length, data, length);
        buffer->length += length;
    }
    return true;
}

static bool rpc_buffer_byte(rpc_buffer_t *buffer, uint8_t value)
{
    return rpc_buffer_append(buffer, &value, 1);
}

static size_t rpc_encode_unsigned_bytes(uint8_t major, uint64_t value, uint8_t output[9])
{
    size_t i;
    if (value < 24U) {
        output[0] = (uint8_t)((major << 5U) | value);
        return 1;
    }
    if (value <= UINT8_MAX) {
        output[0] = (uint8_t)((major << 5U) | 24U);
        output[1] = (uint8_t)value;
        return 2;
    }
    if (value <= UINT16_MAX) {
        output[0] = (uint8_t)((major << 5U) | 25U);
        output[1] = (uint8_t)(value >> 8U);
        output[2] = (uint8_t)value;
        return 3;
    }
    if (value <= UINT32_MAX) {
        output[0] = (uint8_t)((major << 5U) | 26U);
        for (i = 0; i < 4; ++i) {
            output[1 + i] = (uint8_t)(value >> (24U - i * 8U));
        }
        return 5;
    }
    output[0] = (uint8_t)((major << 5U) | 27U);
    for (i = 0; i < 8; ++i) {
        output[1 + i] = (uint8_t)(value >> (56U - i * 8U));
    }
    return 9;
}

static bool rpc_encode_unsigned(rpc_buffer_t *buffer, uint8_t major, uint64_t value)
{
    uint8_t bytes[9];
    size_t length = rpc_encode_unsigned_bytes(major, value, bytes);
    return rpc_buffer_append(buffer, bytes, length);
}

static int rpc_field_id(const rpc_codec_slot_t *codec,
                        const char *name,
                        uint32_t *out_id)
{
    size_t i;
    if (codec == NULL || name == NULL || out_id == NULL) {
        return -1;
    }
    for (i = 0; i < codec->field_count; ++i) {
        if (strcmp(name, codec->fields[i]) == 0) {
            *out_id = (uint32_t)i;
            return 0;
        }
    }
    return -1;
}

static bool rpc_field_is_dynamic(const rpc_codec_slot_t *codec,
                                 uint32_t field_id)
{
    return codec != NULL && field_id < codec->field_count &&
           codec->dynamic_fields[field_id];
}

static int rpc_map_key_compare(const void *left_value, const void *right_value)
{
    const rpc_map_key_t *left = left_value;
    const rpc_map_key_t *right = right_value;
    uint8_t left_prefix[9];
    uint8_t right_prefix[9];
    size_t left_prefix_len;
    size_t right_prefix_len;
    int compared;
    if (left->encoded_key_len != right->encoded_key_len) {
        return left->encoded_key_len < right->encoded_key_len ? -1 : 1;
    }
    left_prefix_len = left->integer_key
        ? rpc_encode_unsigned_bytes(0, left->field_id, left_prefix)
        : rpc_encode_unsigned_bytes(3, strlen(left->name), left_prefix);
    right_prefix_len = right->integer_key
        ? rpc_encode_unsigned_bytes(0, right->field_id, right_prefix)
        : rpc_encode_unsigned_bytes(3, strlen(right->name), right_prefix);
    compared = memcmp(left_prefix, right_prefix,
                      left_prefix_len < right_prefix_len
                          ? left_prefix_len : right_prefix_len);
    if (compared == 0 && left_prefix_len != right_prefix_len) {
        compared = left_prefix_len < right_prefix_len ? -1 : 1;
    }
    if (compared == 0 && !left->integer_key && !right->integer_key) {
        compared = strcmp(left->name, right->name);
    }
    return compared < 0 ? -1 : compared > 0 ? 1 : 0;
}

static bool rpc_encode_value(JSContext *ctx,
                             const rpc_codec_slot_t *codec,
                             JSValue value,
                             rpc_buffer_t *buffer,
                             unsigned int depth,
                             bool string_keys_allowed);

static bool rpc_encode_array(JSContext *ctx,
                             const rpc_codec_slot_t *codec,
                             JSValue value,
                             rpc_buffer_t *buffer,
                             unsigned int depth,
                             bool string_keys_allowed)
{
    JSGCRef length_ref;
    JSValue *length_value = JS_PushGCRef(ctx, &length_ref);
    uint32_t length;
    uint32_t i;

    *length_value = JS_GetPropertyStr(ctx, value, "length");
    if (JS_IsException(*length_value) || JS_ToUint32(ctx, &length, *length_value) != 0 ||
        !rpc_encode_unsigned(buffer, 4, length)) {
        JS_PopGCRef(ctx, &length_ref);
        return false;
    }
    JS_PopGCRef(ctx, &length_ref);
    for (i = 0; i < length; ++i) {
        JSGCRef item_ref;
        JSValue *item = JS_PushGCRef(ctx, &item_ref);
        *item = JS_GetPropertyUint32(ctx, value, i);
        if (JS_IsException(*item) ||
            !rpc_encode_value(ctx, codec, *item, buffer, depth + 1U,
                              string_keys_allowed)) {
            JS_PopGCRef(ctx, &item_ref);
            return false;
        }
        JS_PopGCRef(ctx, &item_ref);
    }
    return true;
}

static void rpc_free_map_keys(rpc_map_key_t *keys, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i) {
        free(keys[i].name);
    }
    free(keys);
}

static bool rpc_encode_map(JSContext *ctx,
                           const rpc_codec_slot_t *codec,
                           JSValue value,
                           rpc_buffer_t *buffer,
                           unsigned int depth,
                           bool string_keys_allowed)
{
    JSGCRef keys_ref;
    JSGCRef length_ref;
    JSValue *keys_array = JS_PushGCRef(ctx, &keys_ref);
    JSValue *length_value = JS_PushGCRef(ctx, &length_ref);
    rpc_map_key_t *keys = NULL;
    uint32_t length = 0;
    uint32_t i;
    bool ok = false;

    *keys_array = esp32_mquickjs_own_property_keys(ctx, value);
    *length_value = JS_IsException(*keys_array)
                        ? JS_EXCEPTION
                        : JS_GetPropertyStr(ctx, *keys_array, "length");
    if (JS_IsException(*length_value) ||
        JS_ToUint32(ctx, &length, *length_value) != 0 || length > 1024U) {
        goto done;
    }
    keys = calloc(length == 0 ? 1U : length, sizeof(*keys));
    if (keys == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto done;
    }
    for (i = 0; i < length; ++i) {
        JSGCRef key_ref;
        JSValue *key_value = JS_PushGCRef(ctx, &key_ref);
        JSCStringBuf key_buf;
        const char *key;
        size_t key_len;

        *key_value = JS_GetPropertyUint32(ctx, *keys_array, i);
        key = JS_IsException(*key_value)
                  ? NULL
                  : JS_ToCStringLen(ctx, &key_len, *key_value, &key_buf);
        if (key == NULL || memchr(key, '\0', key_len) != NULL) {
            if (key != NULL && !JS_HasException(ctx)) {
                JS_ThrowTypeError(ctx,
                                  "RPCCodec.encode() map keys must not contain NUL");
            }
            JS_PopGCRef(ctx, &key_ref);
            goto done;
        }
        keys[i].name = malloc(key_len + 1U);
        if (keys[i].name == NULL) {
            JS_PopGCRef(ctx, &key_ref);
            JS_ThrowOutOfMemory(ctx);
            goto done;
        }
        memcpy(keys[i].name, key, key_len + 1U);
        keys[i].integer_key =
            rpc_field_id(codec, key, &keys[i].field_id) == 0;
        if (!keys[i].integer_key && !string_keys_allowed) {
            JS_PopGCRef(ctx, &key_ref);
            JS_ThrowTypeError(ctx,
                              "RPCCodec.encode() schema field has no integer key: %s",
                              keys[i].name);
            goto done;
        }
        if (keys[i].integer_key) {
            keys[i].encoded_key_len = rpc_encode_unsigned_bytes(
                0, keys[i].field_id, keys[i].encoded_key);
        } else {
            uint8_t prefix[9];
            size_t prefix_len = rpc_encode_unsigned_bytes(3, key_len, prefix);
            keys[i].encoded_key_len = prefix_len + key_len;
        }
        JS_PopGCRef(ctx, &key_ref);
    }
    if (string_keys_allowed) {
        for (i = 0; i < length; ++i) {
            uint32_t j;
            char field_id_key[16];
            int field_id_key_len;

            if (!keys[i].integer_key) {
                continue;
            }
            field_id_key_len = snprintf(field_id_key, sizeof(field_id_key),
                                        "%lu", (unsigned long)keys[i].field_id);
            if (field_id_key_len < 0 ||
                (size_t)field_id_key_len >= sizeof(field_id_key)) {
                JS_ThrowInternalError(ctx,
                                      "RPCCodec.encode() could not format a field id");
                goto done;
            }
            for (j = 0; j < length; ++j) {
                if (strcmp(keys[j].name, field_id_key) == 0) {
                    uint8_t prefix[9];
                    size_t key_len = strlen(keys[i].name);
                    size_t prefix_len =
                        rpc_encode_unsigned_bytes(3, key_len, prefix);
                    keys[i].integer_key = false;
                    keys[i].encoded_key_len = prefix_len + key_len;
                    break;
                }
            }
        }
    }
    qsort(keys, length, sizeof(*keys), rpc_map_key_compare);
    if (!rpc_encode_unsigned(buffer, 5, length)) {
        goto done;
    }
    for (i = 0; i < length; ++i) {
        JSGCRef item_ref;
        JSValue *item = JS_PushGCRef(ctx, &item_ref);
        if (keys[i].integer_key) {
            if (!rpc_encode_unsigned(buffer, 0, keys[i].field_id)) {
                JS_PopGCRef(ctx, &item_ref);
                goto done;
            }
        } else {
            size_t key_len = strlen(keys[i].name);
            if (!rpc_encode_unsigned(buffer, 3, key_len) ||
                !rpc_buffer_append(buffer, keys[i].name, key_len)) {
                JS_PopGCRef(ctx, &item_ref);
                goto done;
            }
        }
        *item = JS_GetPropertyStr(ctx, value, keys[i].name);
        if (JS_IsException(*item) ||
            !rpc_encode_value(
                ctx, codec, *item, buffer, depth + 1U,
                string_keys_allowed ||
                    (keys[i].integer_key &&
                     rpc_field_is_dynamic(codec, keys[i].field_id)))) {
            JS_PopGCRef(ctx, &item_ref);
            goto done;
        }
        JS_PopGCRef(ctx, &item_ref);
    }
    ok = true;

done:
    rpc_free_map_keys(keys, length);
    JS_PopGCRef(ctx, &length_ref);
    JS_PopGCRef(ctx, &keys_ref);
    return ok;
}

static bool rpc_encode_value(JSContext *ctx,
                             const rpc_codec_slot_t *codec,
                             JSValue value,
                             rpc_buffer_t *buffer,
                             unsigned int depth,
                             bool string_keys_allowed)
{
    if (depth > RPC_CBOR_DEPTH) {
        JS_ThrowRangeError(ctx, "RPCCodec.encode() payload exceeds the nesting limit");
        return false;
    }
    if (JS_IsNull(value)) {
        return rpc_buffer_byte(buffer, 0xf6);
    }
    if (JS_IsBool(value)) {
        return rpc_buffer_byte(buffer,
                               JS_VALUE_GET_SPECIAL_VALUE(value) ? 0xf5 : 0xf4);
    }
    if (JS_IsNumber(ctx, value)) {
        double number;
        if (JS_ToNumber(ctx, &number, value) != 0 || !isfinite(number)) {
            JS_ThrowTypeError(ctx, "RPCCodec.encode() requires finite numbers");
            return false;
        }
        if (floor(number) == number && fabs(number) <= RPC_SAFE_INTEGER) {
            uint64_t encoded = number >= 0 ? (uint64_t)number : (uint64_t)(-1.0 - number);
            return rpc_encode_unsigned(buffer, number >= 0 ? 0 : 1, encoded);
        }
        {
            union { double number; uint64_t bits; } raw = { .number = number };
            uint8_t bytes[9];
            size_t i;
            bytes[0] = 0xfb;
            for (i = 0; i < 8; ++i) {
                bytes[1 + i] = (uint8_t)(raw.bits >> (56U - i * 8U));
            }
            return rpc_buffer_append(buffer, bytes, sizeof(bytes));
        }
    }
    if (JS_IsString(ctx, value)) {
        JSCStringBuf string_buf;
        size_t length = 0;
        const char *string = JS_ToCStringLen(ctx, &length, value, &string_buf);
        if (string == NULL) {
            return false;
        }
        return rpc_encode_unsigned(buffer, 3, length) &&
               rpc_buffer_append(buffer, string, length);
    }
    if (JS_GetClassID(ctx, value) == JS_CLASS_BYTE_SPAN_SOURCE ||
        JS_GetClassID(ctx, value) == JS_CLASS_BITMAP_SPAN_SOURCE) {
        size_t length = 0;
        if (buffer->has_stream ||
            !esp32_mquickjs_byte_span_source_known_length(ctx, value, &length) ||
            length > ESP32_MQUICKJS_RPC_STREAM_BYTES ||
            !rpc_encode_unsigned(buffer, 2, length)) {
            if (!JS_HasException(ctx)) {
                JS_ThrowTypeError(
                    ctx,
                    "RPCCodec.encode() requires one final ByteSpanSource with a known length");
            }
            return false;
        }
        buffer->has_stream = true;
        buffer->stream_length = length;
        buffer->stream_value = value;
        buffer->sealed = true;
        return true;
    }
    if (JS_GetClassID(ctx, value) == JS_CLASS_BYTE_VIEW) {
        const uint8_t *data = NULL;
        size_t length = 0;
        bool ok;
        if (!esp32_mquickjs_byte_view_acquire_read(ctx, value, "RPCCodec.encode()",
                                                   &data, &length)) {
            return false;
        }
        ok = rpc_encode_unsigned(buffer, 2, length) &&
             rpc_buffer_append(buffer, data, length);
        esp32_mquickjs_byte_view_release_read(ctx, value);
        return ok;
    }
    if (JS_IsArray(ctx, value)) {
        return rpc_encode_array(ctx, codec, value, buffer, depth,
                                string_keys_allowed);
    }
    if (JS_GetClassID(ctx, value) == JS_CLASS_OBJECT) {
        return rpc_encode_map(ctx, codec, value, buffer, depth,
                              string_keys_allowed);
    }
    JS_ThrowTypeError(ctx, "RPCCodec.encode() payload contains an unsupported value");
    return false;
}

static bool rpc_decode_unsigned(const uint8_t *data,
                                size_t length,
                                size_t *offset,
                                uint8_t additional,
                                uint64_t *out)
{
    size_t bytes;
    size_t i;
    uint64_t value = 0;
    uint64_t minimum;
    if (additional < 24U) {
        *out = additional;
        return true;
    }
    if (additional == 24U) { bytes = 1; minimum = 24U; }
    else if (additional == 25U) { bytes = 2; minimum = 256U; }
    else if (additional == 26U) { bytes = 4; minimum = 65536U; }
    else if (additional == 27U) { bytes = 8; minimum = UINT64_C(4294967296); }
    else return false;
    if (bytes > length - *offset) return false;
    for (i = 0; i < bytes; ++i) value = (value << 8U) | data[(*offset)++];
    if (value < minimum) return false;
    *out = value;
    return true;
}

static JSValue rpc_decode_value(JSContext *ctx,
                                const rpc_codec_slot_t *codec,
                                const uint8_t *data,
                                size_t length,
                                size_t logical_length,
                                size_t *offset,
                                unsigned int depth,
                                bool string_keys_allowed,
                                JSValue stream_value,
                                bool *stream_used);

static JSValue rpc_decode_array(JSContext *ctx,
                                const rpc_codec_slot_t *codec,
                                const uint8_t *data,
                                size_t length,
                                size_t logical_length,
                                size_t *offset,
                                uint64_t count,
                                unsigned int depth,
                                bool string_keys_allowed,
                                JSValue stream_value,
                                bool *stream_used)
{
    JSGCRef array_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    uint64_t i;
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array) || count > UINT32_MAX) goto failed;
    for (i = 0; i < count; ++i) {
        JSGCRef item_ref;
        JSValue *item = JS_PushGCRef(ctx, &item_ref);
        *item = rpc_decode_value(ctx, codec, data, length, logical_length, offset,
                                 depth + 1U, string_keys_allowed,
                                 stream_value, stream_used);
        if (JS_IsException(*item) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *array, (uint32_t)i, *item))) {
            JS_PopGCRef(ctx, &item_ref);
            goto failed;
        }
        JS_PopGCRef(ctx, &item_ref);
    }
    return JS_PopGCRef(ctx, &array_ref);
failed:
    JS_PopGCRef(ctx, &array_ref);
    return JS_EXCEPTION;
}

static bool rpc_key_order(const uint8_t *previous,
                          size_t previous_len,
                          const uint8_t *current,
                          size_t current_len)
{
    if (previous_len == 0) return true;
    if (previous_len != current_len) return previous_len < current_len;
    return memcmp(previous, current, current_len) < 0;
}

static JSValue rpc_decode_map(JSContext *ctx,
                              const rpc_codec_slot_t *codec,
                              const uint8_t *data,
                              size_t length,
                              size_t logical_length,
                              size_t *offset,
                              uint64_t count,
                              unsigned int depth,
                              bool string_keys_allowed,
                              JSValue stream_value,
                              bool *stream_used)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    size_t previous_key_start = 0;
    size_t previous_key_len = 0;
    uint64_t i;
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) || count > 1024U) goto failed;
    for (i = 0; i < count; ++i) {
        size_t key_start = *offset;
        uint8_t initial;
        uint8_t major;
        uint8_t additional;
        uint64_t raw;
        bool dynamic_field = false;
        const char *field = NULL;
        char *owned_key = NULL;
        const char *key;
        size_t key_len;
        JSGCRef item_ref;
        JSValue *item;

        if (*offset >= length) goto failed;
        initial = data[(*offset)++];
        major = initial >> 5U;
        additional = initial & 0x1fU;
        if (major == 0) {
            if (!rpc_decode_unsigned(data, length, offset, additional, &raw) ||
                codec == NULL || raw >= codec->field_count) goto failed;
            field = codec->fields[raw];
            dynamic_field = rpc_field_is_dynamic(codec, (uint32_t)raw);
            key = field;
        } else if (major == 3 && string_keys_allowed) {
            if (!rpc_decode_unsigned(data, length, offset, additional, &raw) ||
                raw > length - *offset) goto failed;
            owned_key = malloc((size_t)raw + 1U);
            if (owned_key == NULL) {
                JS_ThrowOutOfMemory(ctx);
                goto failed;
            }
            memcpy(owned_key, data + *offset, (size_t)raw);
            owned_key[raw] = '\0';
            *offset += (size_t)raw;
            key = owned_key;
        } else goto failed;
        key_len = *offset - key_start;
        if (previous_key_len != 0 &&
            !rpc_key_order(data + previous_key_start, previous_key_len,
                           data + key_start, key_len)) {
            free(owned_key);
            goto failed;
        }
        previous_key_start = key_start;
        previous_key_len = key_len;
        item = JS_PushGCRef(ctx, &item_ref);
        *item = rpc_decode_value(
            ctx, codec, data, length, logical_length, offset, depth + 1U,
            string_keys_allowed || dynamic_field, stream_value, stream_used);
        if (JS_IsException(*item) ||
            JS_IsException(JS_SetPropertyStr(ctx, *object, key, *item))) {
            JS_PopGCRef(ctx, &item_ref);
            free(owned_key);
            goto failed;
        }
        JS_PopGCRef(ctx, &item_ref);
        free(owned_key);
    }
    return JS_PopGCRef(ctx, &object_ref);
failed:
    if (!JS_HasException(ctx)) {
        JS_ThrowTypeError(ctx, "RPCDecoder.feed() received invalid deterministic CBOR");
    }
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

static JSValue rpc_decode_value(JSContext *ctx,
                                const rpc_codec_slot_t *codec,
                                const uint8_t *data,
                                size_t length,
                                size_t logical_length,
                                size_t *offset,
                                unsigned int depth,
                                bool string_keys_allowed,
                                JSValue stream_value,
                                bool *stream_used)
{
    uint8_t initial;
    uint8_t major;
    uint8_t additional;
    uint64_t value;
    if (depth > RPC_CBOR_DEPTH || *offset >= length) goto invalid;
    initial = data[(*offset)++];
    major = initial >> 5U;
    additional = initial & 0x1fU;
    if (major == 0 || major == 1) {
        double number;
        if (!rpc_decode_unsigned(data, length, offset, additional, &value) ||
            value > UINT64_C(9007199254740991)) goto invalid;
        number = major == 0 ? (double)value : -1.0 - (double)value;
        return JS_NewFloat64(ctx, number);
    }
    if (major == 2 || major == 3) {
        if (!rpc_decode_unsigned(data, length, offset, additional, &value)) goto invalid;
        if (value > length - *offset) {
            if (major != 2 || JS_IsUndefined(stream_value) || stream_used == NULL ||
                *stream_used || *offset != length ||
                value != logical_length - length) goto invalid;
            *stream_used = true;
            *offset = logical_length;
            return stream_value;
        }
        if (major == 3) {
            JSValue result;
            result = JS_NewStringLen(ctx, (const char *)data + *offset, (size_t)value);
            *offset += (size_t)value;
            return result;
        } else {
            uint8_t *copy = value == 0 ? NULL : malloc((size_t)value);
            if (value != 0 && copy == NULL) return JS_ThrowOutOfMemory(ctx);
            if (value != 0) memcpy(copy, data + *offset, (size_t)value);
            *offset += (size_t)value;
            return esp32_mquickjs_new_owned_byte_view(ctx, copy, (size_t)value);
        }
    }
    if (major == 4 || major == 5) {
        if (!rpc_decode_unsigned(data, length, offset, additional, &value)) goto invalid;
        return major == 4
                   ? rpc_decode_array(ctx, codec, data, length, logical_length,
                                      offset, value, depth, string_keys_allowed,
                                      stream_value, stream_used)
                   : rpc_decode_map(ctx, codec, data, length, logical_length,
                                    offset, value, depth, string_keys_allowed,
                                    stream_value,
                                    stream_used);
    }
    if (major == 7) {
        if (additional == 20U) return JS_NewBool(false);
        if (additional == 21U) return JS_NewBool(true);
        if (additional == 22U) return JS_NULL;
        if (additional == 27U) {
            union { uint64_t bits; double number; } raw = {0};
            size_t i;
            if (8U > length - *offset) goto invalid;
            for (i = 0; i < 8; ++i) raw.bits = (raw.bits << 8U) | data[(*offset)++];
            if (!isfinite(raw.number)) goto invalid;
            return JS_NewFloat64(ctx, raw.number);
        }
    }
invalid:
    return JS_ThrowTypeError(ctx, "RPCDecoder.feed() received invalid deterministic CBOR");
}

static JSValue rpc_decode_payload(JSContext *ctx,
                                  const rpc_codec_slot_t *codec,
                                  const uint8_t *data,
                                  size_t length,
                                  size_t logical_length,
                                  JSValue stream_value)
{
    CborParser parser;
    CborValue root;
    CborError validation_error;
    uint32_t validation_flags = CborValidateUtf8;
    size_t offset = 0;
    bool stream_used = false;
    JSValue result;
    if (cbor_parser_init(data, length, 0, &parser, &root) != CborNoError) {
        return JS_ThrowTypeError(ctx, "RPCDecoder.feed() received malformed CBOR");
    }
    if (logical_length == length) {
        validation_flags |= (uint32_t)CborValidateCompleteData;
    }
    /* Text well-formedness is part of the CBOR wire type, not product policy. */
    validation_error = cbor_value_validate(&root, validation_flags);
    if ((logical_length == length && validation_error != CborNoError) ||
        (logical_length != length &&
         validation_error != CborErrorUnexpectedEOF)) {
        if (validation_error == CborErrorInvalidUtf8TextString) {
            return JS_ThrowTypeError(ctx,
                                     "RPCDecoder.feed() received invalid CBOR text");
        }
        return JS_ThrowTypeError(ctx, "RPCDecoder.feed() received malformed CBOR");
    }
    result = rpc_decode_value(ctx, codec, data, length, logical_length, &offset, 0,
                              codec->allow_string_keys, stream_value,
                              &stream_used);
    if (JS_IsException(result)) return result;
    if (offset != logical_length ||
        (logical_length != length && !stream_used) ||
        (logical_length == length && stream_used)) {
        return JS_ThrowTypeError(ctx, "RPCDecoder.feed() received trailing CBOR data");
    }
    return result;
}

static void rpc_codec_cleanup(rpc_codec_slot_t *codec)
{
    size_t i;
    uint32_t generation;

    if (codec == NULL) {
        return;
    }
    generation = codec->generation;
    for (i = 0; i < codec->field_count; ++i) {
        free(codec->fields[i]);
    }
    free(codec->fields);
    free(codec->dynamic_fields);
    memset(codec, 0, sizeof(*codec));
    codec->generation = generation;
}

static rpc_codec_slot_t *rpc_codec_from_value(JSContext *ctx,
                                               JSValue value,
                                               const char *api_name)
{
    rpc_handle_ref_t *ref;
    rpc_codec_slot_t *codec;

    if (JS_GetClassID(ctx, value) != JS_CLASS_RPC_CODEC ||
        (ref = JS_GetOpaque(ctx, value)) == NULL ||
        ref->slot >= RPC_CODEC_SLOTS) {
        JS_ThrowTypeError(ctx, "%s expects an RPCCodec object", api_name);
        return NULL;
    }
    codec = &s_rpc_codecs[ref->slot];
    if (!codec->used || codec->generation != ref->generation ||
        codec->close_pending) {
        JS_ThrowReferenceError(ctx, "%s cannot use a closed or stale RPCCodec",
                               api_name);
        return NULL;
    }
    return codec;
}

static JSValue rpc_new_handle(JSContext *ctx,
                              int class_id,
                              uint8_t slot,
                              uint32_t generation)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    rpc_handle_ref_t *ref;

    *object = JS_NewObjectClassUser(ctx, class_id);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    ref = calloc(1, sizeof(*ref));
    if (ref == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    ref->slot = slot;
    ref->generation = generation;
    JS_SetOpaque(ctx, *object, ref);
    return JS_PopGCRef(ctx, &object_ref);
}

static bool rpc_codec_copy_fields(JSContext *ctx,
                                  rpc_codec_slot_t *codec,
                                  JSValue fields_value)
{
    JSGCRef length_ref;
    JSValue *length_value = JS_PushGCRef(ctx, &length_ref);
    uint32_t count;
    uint32_t i;
    size_t total_bytes = 0;

    *length_value = JS_GetPropertyStr(ctx, fields_value, "length");
    if (!JS_IsArray(ctx, fields_value) || JS_IsException(*length_value) ||
        JS_ToUint32(ctx, &count, *length_value) != 0 ||
        count == 0 || count > RPC_CODEC_FIELDS) {
        JS_PopGCRef(ctx, &length_ref);
        JS_ThrowTypeError(ctx,
                          "rpc.createCodec() fields must be a non-empty bounded array");
        return false;
    }
    JS_PopGCRef(ctx, &length_ref);
    codec->fields = calloc(count, sizeof(*codec->fields));
    codec->dynamic_fields = calloc(count, sizeof(*codec->dynamic_fields));
    if (codec->fields == NULL || codec->dynamic_fields == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    codec->field_count = count;
    for (i = 0; i < count; ++i) {
        JSGCRef field_ref;
        JSValue *field_value = JS_PushGCRef(ctx, &field_ref);
        JSCStringBuf field_buf;
        const char *field;
        size_t field_len = 0;
        uint32_t previous;

        *field_value = JS_GetPropertyUint32(ctx, fields_value, i);
        field = JS_IsString(ctx, *field_value)
                    ? JS_ToCStringLen(ctx, &field_len, *field_value, &field_buf)
                    : NULL;
        if (field == NULL || field_len == 0 || field_len > RPC_CODEC_FIELD_BYTES ||
            strlen(field) != field_len ||
            field_len > RPC_CODEC_TOTAL_FIELD_BYTES - total_bytes) {
            JS_PopGCRef(ctx, &field_ref);
            JS_ThrowTypeError(ctx,
                              "rpc.createCodec() fields must contain bounded unique strings");
            return false;
        }
        for (previous = 0; previous < i; ++previous) {
            if (strcmp(codec->fields[previous], field) == 0) {
                JS_PopGCRef(ctx, &field_ref);
                JS_ThrowTypeError(ctx,
                                  "rpc.createCodec() fields must contain bounded unique strings");
                return false;
            }
        }
        codec->fields[i] = malloc(field_len + 1U);
        if (codec->fields[i] == NULL) {
            JS_PopGCRef(ctx, &field_ref);
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
        memcpy(codec->fields[i], field, field_len + 1U);
        total_bytes += field_len;
        JS_PopGCRef(ctx, &field_ref);
    }
    return true;
}

static bool rpc_codec_mark_dynamic_fields(JSContext *ctx,
                                          rpc_codec_slot_t *codec,
                                          JSValue dynamic_value)
{
    JSGCRef length_ref;
    JSValue *length_value;
    uint32_t count;
    uint32_t i;

    if (JS_IsUndefined(dynamic_value)) {
        return true;
    }
    length_value = JS_PushGCRef(ctx, &length_ref);
    *length_value = JS_GetPropertyStr(ctx, dynamic_value, "length");
    if (!JS_IsArray(ctx, dynamic_value) || JS_IsException(*length_value) ||
        JS_ToUint32(ctx, &count, *length_value) != 0 ||
        count > codec->field_count) {
        JS_PopGCRef(ctx, &length_ref);
        JS_ThrowTypeError(ctx,
                          "rpc.createCodec() dynamicFields must be a bounded array");
        return false;
    }
    JS_PopGCRef(ctx, &length_ref);
    for (i = 0; i < count; ++i) {
        JSGCRef field_ref;
        JSValue *field_value = JS_PushGCRef(ctx, &field_ref);
        JSCStringBuf field_buf;
        const char *field;
        uint32_t field_id;

        *field_value = JS_GetPropertyUint32(ctx, dynamic_value, i);
        field = JS_IsString(ctx, *field_value)
                    ? JS_ToCString(ctx, *field_value, &field_buf)
                    : NULL;
        if (field == NULL || rpc_field_id(codec, field, &field_id) != 0 ||
            codec->dynamic_fields[field_id]) {
            JS_PopGCRef(ctx, &field_ref);
            JS_ThrowTypeError(ctx,
                              "rpc.createCodec() dynamicFields must name unique fields");
            return false;
        }
        codec->dynamic_fields[field_id] = true;
        JS_PopGCRef(ctx, &field_ref);
    }
    return true;
}

JSValue js_rpc_create_codec(JSContext *ctx,
                            JSValue *this_val,
                            int argc,
                            JSValue *argv)
{
    rpc_codec_slot_t *codec = NULL;
    JSGCRef fields_ref;
    JSGCRef dynamic_ref;
    JSGCRef directory_ref;
    JSGCRef string_keys_ref;
    JSValue *fields = JS_PushGCRef(ctx, &fields_ref);
    JSValue *dynamic_fields = JS_PushGCRef(ctx, &dynamic_ref);
    JSValue *stream_directory = JS_PushGCRef(ctx, &directory_ref);
    JSValue *string_keys = JS_PushGCRef(ctx, &string_keys_ref);
    size_t slot;
    JSValue result = JS_EXCEPTION;

    (void)this_val;
    if (argc != 1 || JS_GetClassID(ctx, argv[0]) != JS_CLASS_OBJECT ||
        JS_IsArray(ctx, argv[0])) {
        JS_ThrowTypeError(ctx, "rpc.createCodec(options) expects an object");
        goto done;
    }
    for (slot = 0; slot < RPC_CODEC_SLOTS; ++slot) {
        if (!s_rpc_codecs[slot].used) {
            uint32_t generation = s_rpc_codecs[slot].generation + 1U;

            if (generation == 0) {
                generation = 1;
            }
            codec = &s_rpc_codecs[slot];
            memset(codec, 0, sizeof(*codec));
            codec->generation = generation;
            codec->used = true;
            break;
        }
    }
    if (codec == NULL) {
        JS_ThrowInternalError(ctx, "rpc.createCodec() has no free codec slot");
        goto done;
    }
    *fields = JS_GetPropertyStr(ctx, argv[0], "fields");
    *dynamic_fields = JS_GetPropertyStr(ctx, argv[0], "dynamicFields");
    *stream_directory = JS_GetPropertyStr(ctx, argv[0], "streamDirectory");
    *string_keys = JS_GetPropertyStr(ctx, argv[0], "allowStringKeys");
    if (JS_IsException(*fields) || JS_IsException(*dynamic_fields) ||
        JS_IsException(*stream_directory) || JS_IsException(*string_keys) ||
        !rpc_codec_copy_fields(ctx, codec, *fields) ||
        !rpc_codec_mark_dynamic_fields(ctx, codec, *dynamic_fields)) {
        goto failed;
    }
    if (!JS_IsUndefined(*string_keys)) {
        if (!JS_IsBool(*string_keys)) {
            JS_ThrowTypeError(ctx, "rpc.createCodec() allowStringKeys must be boolean");
            goto failed;
        }
        codec->allow_string_keys = JS_VALUE_GET_SPECIAL_VALUE(*string_keys) != 0;
    }
    if (!JS_IsUndefined(*stream_directory)) {
        JSCStringBuf directory_buf;
        size_t directory_len = 0;
        const char *directory = JS_IsString(ctx, *stream_directory)
                                    ? JS_ToCStringLen(ctx, &directory_len,
                                                     *stream_directory,
                                                     &directory_buf)
                                    : NULL;
        if (directory == NULL || directory_len == 0 ||
            directory_len >= sizeof(codec->stream_directory) ||
            strlen(directory) != directory_len) {
            JS_ThrowTypeError(ctx,
                              "rpc.createCodec() streamDirectory must be a bounded path");
            goto failed;
        }
        while (directory_len > 1U && directory[directory_len - 1U] == '/') {
            directory_len--;
        }
        memcpy(codec->stream_directory, directory, directory_len);
        codec->stream_directory[directory_len] = '\0';
    }
    result = rpc_new_handle(ctx, JS_CLASS_RPC_CODEC, (uint8_t)slot,
                            codec->generation);
    if (JS_IsException(result)) {
        goto failed;
    }
    goto done;

failed:
    rpc_codec_cleanup(codec);
done:
    JS_PopGCRef(ctx, &string_keys_ref);
    JS_PopGCRef(ctx, &directory_ref);
    JS_PopGCRef(ctx, &dynamic_ref);
    JS_PopGCRef(ctx, &fields_ref);
    return result;
}

JSValue js_rpc_codec_close(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv)
{
    rpc_handle_ref_t *ref;
    rpc_codec_slot_t *codec;

    (void)argc;
    (void)argv;
    if (this_val == NULL || JS_GetClassID(ctx, *this_val) != JS_CLASS_RPC_CODEC) {
        return JS_ThrowTypeError(ctx, "RPCCodec.close() expects an RPCCodec object");
    }
    ref = JS_GetOpaque(ctx, *this_val);
    if (ref == NULL) {
        return JS_FALSE;
    }
    codec = rpc_codec_from_value(ctx, *this_val, "RPCCodec.close()");
    if (codec == NULL) {
        return JS_EXCEPTION;
    }
    if (codec->decoders != 0) {
        return JS_ThrowReferenceError(ctx,
                                      "RPCCodec.close() codec still has active decoders");
    }
    rpc_codec_cleanup(codec);
    JS_SetOpaque(ctx, *this_val, NULL);
    free(ref);
    return JS_NewBool(true);
}

JSValue js_rpc_codec_constructor(JSContext *ctx,
                                 JSValue *this_val,
                                 int argc,
                                 JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "RPCCodec cannot be constructed directly");
}

void js_rpc_codec_finalizer(JSContext *ctx, void *opaque)
{
    rpc_handle_ref_t *ref = opaque;
    rpc_codec_slot_t *codec;

    (void)ctx;
    if (ref == NULL || ref->slot >= RPC_CODEC_SLOTS) {
        free(ref);
        return;
    }
    codec = &s_rpc_codecs[ref->slot];
    if (codec->used && codec->generation == ref->generation) {
        if (codec->decoders == 0) {
            rpc_codec_cleanup(codec);
        } else {
            codec->close_pending = true;
        }
    }
    free(ref);
}

static rpc_decoder_slot_t *rpc_decoder_from_value(JSContext *ctx,
                                                   JSValue value,
                                                   const char *api_name)
{
    rpc_handle_ref_t *ref;
    rpc_decoder_slot_t *decoder;

    if (JS_GetClassID(ctx, value) != JS_CLASS_RPC_DECODER ||
        (ref = JS_GetOpaque(ctx, value)) == NULL ||
        ref->slot >= RPC_DECODER_SLOTS) {
        JS_ThrowTypeError(ctx, "%s expects an RPCDecoder object", api_name);
        return NULL;
    }
    decoder = &s_rpc_decoders[ref->slot];
    if (!decoder->used || decoder->generation != ref->generation) {
        JS_ThrowReferenceError(
            ctx, "%s cannot use a closed or stale RPCDecoder", api_name);
        return NULL;
    }
    return decoder;
}

static void rpc_decoder_cleanup(rpc_decoder_slot_t *slot)
{
    rpc_codec_slot_t *codec;
    uint32_t generation;

    if (slot == NULL || !slot->used) {
        return;
    }
    codec = slot->codec;
    generation = slot->generation;
    rpc_decoder_stream_cleanup(slot);
    esp32_mquickjs_rpc_wire_decoder_reset(&slot->wire);
    memset(slot, 0, sizeof(*slot));
    slot->generation = generation;
    if (codec != NULL && codec->decoders != 0) {
        codec->decoders--;
    }
    if (codec != NULL && codec->close_pending && codec->decoders == 0) {
        rpc_codec_cleanup(codec);
    }
}

static bool rpc_decoder_stream_begin(rpc_decoder_slot_t *slot,
                                     uint32_t request_id,
                                     size_t expected)
{
    int path_length;
    size_t codec_id;
    size_t slot_id;

    if (slot == NULL || slot->codec == NULL ||
        slot->codec->stream_directory[0] == '\0' ||
        slot->stream_file != NULL || slot->stream_path[0] != '\0') {
        return false;
    }
    codec_id = (size_t)(slot->codec - s_rpc_codecs) + 1U;
    slot_id = (size_t)(slot - s_rpc_decoders) + 1U;
    path_length = snprintf(slot->stream_path, sizeof(slot->stream_path),
                           "%s/.esp32qjs-rpc-%u-%u-%08lx",
                           slot->codec->stream_directory, (unsigned)codec_id,
                           (unsigned)slot_id, (unsigned long)request_id);
    if (path_length <= 0 || (size_t)path_length >= sizeof(slot->stream_path)) {
        slot->stream_path[0] = '\0';
        return false;
    }
    unlink(slot->stream_path);
    slot->stream_file = fopen(slot->stream_path, "wb");
    if (slot->stream_file == NULL) {
        slot->stream_path[0] = '\0';
        return false;
    }
    slot->stream_expected = expected;
    slot->stream_received = 0;
    slot->stream_crc_state = UINT32_C(0xffffffff);
    return true;
}

static bool rpc_feed_stream(void *opaque,
                            const esp32_mquickjs_rpc_wire_stream_chunk_t *chunk)
{
    rpc_feed_context_t *feed = opaque;
    rpc_decoder_slot_t *slot = feed->slot;

    if (feed->failed || chunk == NULL || slot == NULL) {
        return false;
    }
    if (chunk->first && !rpc_decoder_stream_begin(
            slot, chunk->request_id, chunk->stream_len)) {
        feed->failed = true;
        return false;
    }
    if (slot->stream_file == NULL || slot->stream_expected != chunk->stream_len ||
        slot->stream_received != chunk->offset ||
        chunk->data_len > slot->stream_expected - slot->stream_received ||
        fwrite(chunk->data, 1, chunk->data_len, slot->stream_file) != chunk->data_len) {
        feed->failed = true;
        rpc_decoder_stream_cleanup(slot);
        return false;
    }
    slot->stream_crc_state = esp32_mquickjs_rpc_crc32_update(
        slot->stream_crc_state, chunk->data, chunk->data_len);
    slot->stream_received += chunk->data_len;
    if (chunk->last) {
        if (slot->stream_received != slot->stream_expected ||
            fflush(slot->stream_file) != 0 || fclose(slot->stream_file) != 0) {
            slot->stream_file = NULL;
            feed->failed = true;
            rpc_decoder_stream_cleanup(slot);
            return false;
        }
        slot->stream_file = NULL;
    }
    return true;
}

static void rpc_feed_message(void *opaque,
                             const esp32_mquickjs_rpc_wire_message_t *message)
{
    rpc_feed_context_t *feed = opaque;
    JSGCRef entry_ref;
    JSGCRef payload_ref;
    JSGCRef source_ref;
    JSValue *entry;
    JSValue *payload;
    JSValue *source;
    bool streamed;
    if (feed->failed) return;
    entry = JS_PushGCRef(feed->ctx, &entry_ref);
    payload = JS_PushGCRef(feed->ctx, &payload_ref);
    source = JS_PushGCRef(feed->ctx, &source_ref);
    *entry = JS_NewObject(feed->ctx);
    *source = JS_UNDEFINED;
    streamed = (message->flags & ESP32_MQUICKJS_RPC_FLAG_STREAM) != 0;
    if (streamed) {
        rpc_decoder_slot_t *slot = feed->slot;
        if (message->stream_len == 0 && slot->stream_path[0] == '\0' &&
            !rpc_decoder_stream_begin(slot, message->request_id, 0)) {
            feed->failed = true;
        }
        if (!feed->failed && slot->stream_file != NULL) {
            if (fflush(slot->stream_file) != 0 || fclose(slot->stream_file) != 0) {
                feed->failed = true;
            }
            slot->stream_file = NULL;
        }
        if (!feed->failed &&
            (slot->stream_received != message->stream_len ||
             slot->stream_path[0] == '\0')) {
            feed->failed = true;
        }
        if (!feed->failed) {
            *source = rpc_new_file_source(
                feed->ctx, slot->stream_path, slot->stream_received, true, true,
                slot->stream_crc_state ^ UINT32_C(0xffffffff));
            if (!JS_IsException(*source)) {
                slot->stream_path[0] = '\0';
                slot->stream_received = 0;
                slot->stream_expected = 0;
            }
        }
    }
    *payload = feed->failed || JS_IsException(*source)
        ? JS_EXCEPTION
        : rpc_decode_payload(
            feed->ctx, feed->slot->codec, message->payload,
            message->payload_len, message->logical_len, *source);
    if (JS_IsException(*entry) || JS_IsException(*payload) ||
        !esp32_mquickjs_set_property_ref(feed->ctx, entry, "opcode",
                                         JS_NewUint32(feed->ctx, message->opcode)) ||
        !esp32_mquickjs_set_property_ref(feed->ctx, entry, "requestId",
                                         JS_NewUint32(feed->ctx, message->request_id)) ||
        !esp32_mquickjs_set_property_ref(feed->ctx, entry, "flags",
                                         JS_NewUint32(feed->ctx, message->flags)) ||
        !esp32_mquickjs_set_property_ref(feed->ctx, entry, "logicalLength",
                                         JS_NewUint32(feed->ctx, message->logical_len)) ||
        !esp32_mquickjs_set_property_ref(feed->ctx, entry, "payload", *payload) ||
        JS_IsException(JS_SetPropertyUint32(feed->ctx, *feed->messages,
                                            feed->message_index++, *entry))) {
        feed->failed = true;
    }
    JS_PopGCRef(feed->ctx, &source_ref);
    JS_PopGCRef(feed->ctx, &payload_ref);
    JS_PopGCRef(feed->ctx, &entry_ref);
}

static void rpc_feed_error(void *opaque, esp32_mquickjs_rpc_wire_error_t error)
{
    rpc_feed_context_t *feed = opaque;
    (void)error;
    if (feed != NULL) {
        rpc_decoder_stream_cleanup(feed->slot);
    }
}

JSValue js_rpc_create_decoder(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    rpc_codec_slot_t *codec;
    size_t i;

    (void)argv;
    if (argc != 0 || this_val == NULL ||
        (codec = rpc_codec_from_value(
             ctx, *this_val, "RPCCodec.createDecoder()")) == NULL) {
        return JS_EXCEPTION;
    }
    for (i = 0; i < RPC_DECODER_SLOTS; ++i) {
        if (!s_rpc_decoders[i].used) {
            uint32_t generation = s_rpc_decoders[i].generation + 1U;
            JSValue result;

            if (generation == 0) {
                generation = 1;
            }
            memset(&s_rpc_decoders[i], 0, sizeof(s_rpc_decoders[i]));
            s_rpc_decoders[i].generation = generation;
            s_rpc_decoders[i].used = true;
            s_rpc_decoders[i].codec = codec;
            codec->decoders++;
            esp32_mquickjs_rpc_wire_decoder_init(&s_rpc_decoders[i].wire);
            result = rpc_new_handle(ctx, JS_CLASS_RPC_DECODER, (uint8_t)i,
                                    generation);
            if (JS_IsException(result)) {
                rpc_decoder_cleanup(&s_rpc_decoders[i]);
            }
            return result;
        }
    }
    return JS_ThrowInternalError(
        ctx, "RPCCodec.createDecoder() has no free decoder slot");
}

JSValue js_rpc_decoder_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    rpc_handle_ref_t *ref;
    rpc_decoder_slot_t *slot;

    (void)argc;
    (void)argv;
    if (this_val == NULL || JS_GetClassID(ctx, *this_val) != JS_CLASS_RPC_DECODER) {
        return JS_ThrowTypeError(
            ctx, "RPCDecoder.close() expects an RPCDecoder object");
    }
    ref = JS_GetOpaque(ctx, *this_val);
    if (ref == NULL) {
        return JS_FALSE;
    }
    slot = rpc_decoder_from_value(ctx, *this_val, "RPCDecoder.close()");
    if (slot == NULL) {
        return JS_EXCEPTION;
    }
    rpc_decoder_cleanup(slot);
    JS_SetOpaque(ctx, *this_val, NULL);
    free(ref);
    return JS_NewBool(true);
}

JSValue js_rpc_decoder_constructor(JSContext *ctx,
                                   JSValue *this_val,
                                   int argc,
                                   JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "RPCDecoder cannot be constructed directly");
}

void js_rpc_decoder_finalizer(JSContext *ctx, void *opaque)
{
    rpc_handle_ref_t *ref = opaque;
    rpc_decoder_slot_t *slot;

    (void)ctx;
    if (ref == NULL || ref->slot >= RPC_DECODER_SLOTS) {
        free(ref);
        return;
    }
    slot = &s_rpc_decoders[ref->slot];
    if (slot->used && slot->generation == ref->generation) {
        rpc_decoder_cleanup(slot);
    }
    free(ref);
}

JSValue js_rpc_reset_decoder(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    rpc_decoder_slot_t *slot;

    (void)argv;
    if (argc != 0 || this_val == NULL ||
        (slot = rpc_decoder_from_value(
             ctx, *this_val, "RPCDecoder.reset()")) == NULL) {
        return JS_EXCEPTION;
    }
    rpc_decoder_stream_cleanup(slot);
    esp32_mquickjs_rpc_wire_decoder_reset(&slot->wire);
    esp32_mquickjs_rpc_wire_decoder_init(&slot->wire);
    return JS_NewBool(true);
}

JSValue js_rpc_decoder_status(JSContext *ctx,
                              JSValue *this_val,
                              int argc,
                              JSValue *argv)
{
    rpc_decoder_slot_t *slot;
    JSGCRef result_ref;
    JSValue *result;

    (void)argv;
    if (argc != 0 || this_val == NULL ||
        (slot = rpc_decoder_from_value(
             ctx, *this_val, "RPCDecoder.status()")) == NULL) {
        return JS_EXCEPTION;
    }
    result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "open", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "messages", JS_NewUint32(ctx, slot->messages)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "errors", JS_NewUint32(ctx, slot->errors)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "streamActive",
            JS_NewBool(slot->stream_file != NULL ||
                       slot->stream_path[0] != '\0')) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "streamReceivedBytes",
            JS_NewUint32(ctx, (uint32_t)slot->stream_received)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "streamExpectedBytes",
            JS_NewUint32(ctx, (uint32_t)slot->stream_expected))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

JSValue js_rpc_feed(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    rpc_decoder_slot_t *slot;
    esp32_mquickjs_byte_source_t source;
    uint8_t *owned = NULL;
    JSValue error = JS_UNDEFINED;
    JSGCRef messages_ref;
    JSValue *messages;
    rpc_feed_context_t feed;
    size_t emitted;
    if (argc != 1 || this_val == NULL ||
        (slot = rpc_decoder_from_value(
             ctx, *this_val, "RPCDecoder.feed()")) == NULL) {
        return JS_EXCEPTION;
    }
    if (!esp32_mquickjs_get_byte_source(ctx, argv[0], "RPCDecoder.feed()", &source,
                                        &owned, &error)) {
        return JS_IsUndefined(error) ? JS_EXCEPTION : error;
    }
    messages = JS_PushGCRef(ctx, &messages_ref);
    *messages = JS_NewArray(ctx, 0);
    feed = (rpc_feed_context_t){ .ctx = ctx, .messages = messages, .slot = slot };
    emitted = esp32_mquickjs_rpc_wire_decoder_feed(
        &slot->wire, source.data, source.length,
        (uint64_t)(esp_timer_get_time() / 1000), rpc_feed_message,
        rpc_feed_stream,
        rpc_feed_error, &feed);
    esp32_mquickjs_release_byte_source(owned);
    slot->messages += (uint32_t)emitted;
    if (feed.failed || JS_IsException(*messages)) {
        slot->errors++;
        JS_PopGCRef(ctx, &messages_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &messages_ref);
}

static void rpc_encoded_stream_free(rpc_encoded_stream_object_t *stream)
{
    if (stream == NULL) {
        return;
    }
    if (stream->rooted) {
        JS_DeleteGCRef(stream->ctx, &stream->source_ref);
        stream->rooted = false;
    }
    free(stream->prefix);
    free(stream);
}

static bool rpc_encoded_stream_open(JSContext *ctx,
                                    JSValue source_value,
                                    void *opaque,
                                    esp32_mquickjs_byte_span_source_t *out,
                                    JSValue *out_error)
{
    rpc_encoded_stream_object_t *stream = opaque;

    (void)source_value;
    if (stream == NULL || stream->opened || stream->consumed ||
        stream->destroy_requested) {
        *out_error = JS_ThrowReferenceError(
            ctx, "encoded RPC stream is closed or has already been consumed");
        return false;
    }
    if (!esp32_mquickjs_open_byte_span_source(
            ctx, stream->source_ref.val, "RPCCodec.encode() stream",
            &stream->source, out_error)) {
        return false;
    }
    stream->opened = true;
    out->opaque = stream;
    out->next = NULL;
    out->close = NULL;
    return true;
}

static bool rpc_encoded_stream_load_source(JSContext *ctx,
                                           rpc_encoded_stream_object_t *stream)
{
    unsigned empty = 0;

    while (stream->span_offset >= stream->span.length) {
        esp32_mquickjs_byte_span_clear(&stream->span);
        stream->span_offset = 0;
        if (!esp32_mquickjs_byte_span_source_next(ctx, &stream->source,
                                                  &stream->span)) {
            if (!JS_HasException(ctx) &&
                stream->source_produced != stream->source_len) {
                JS_ThrowInternalError(ctx,
                                      "RPCCodec.encode() ByteSpanSource length changed");
            }
            return false;
        }
        if (stream->span.length == 0) {
            if (++empty > 16) {
                JS_ThrowInternalError(
                    ctx, "RPCCodec.encode() ByteSpanSource yielded too many empty spans");
                return false;
            }
            continue;
        }
        if (stream->span.data == NULL ||
            stream->span.length > stream->source_len - stream->source_produced) {
            JS_ThrowInternalError(ctx,
                                  "RPCCodec.encode() ByteSpanSource length changed");
            return false;
        }
    }
    return true;
}

static bool rpc_encoded_stream_next(JSContext *ctx,
                                    void *opaque,
                                    esp32_mquickjs_byte_span_t *out)
{
    rpc_encoded_stream_object_t *stream = opaque;
    size_t segment_len = 0;
    size_t logical_len;
    size_t wire_len = 0;
    uint8_t flags;

    if (stream == NULL || !stream->opened || stream->consumed) {
        JS_ThrowReferenceError(ctx, "encoded RPC stream iterator is closed");
        return false;
    }
    logical_len = stream->prefix_len + stream->source_len;
    if (stream->logical_offset >= logical_len) {
        return false;
    }
    while (segment_len < sizeof(stream->segment) &&
           stream->logical_offset < stream->prefix_len) {
        size_t length = stream->prefix_len - stream->logical_offset;
        if (length > sizeof(stream->segment) - segment_len) {
            length = sizeof(stream->segment) - segment_len;
        }
        memcpy(stream->segment + segment_len,
               stream->prefix + stream->logical_offset, length);
        segment_len += length;
        stream->logical_offset += length;
    }
    while (segment_len < sizeof(stream->segment) &&
           stream->source_produced < stream->source_len) {
        size_t length;
        if (!rpc_encoded_stream_load_source(ctx, stream)) {
            return false;
        }
        length = stream->span.length - stream->span_offset;
        if (length > sizeof(stream->segment) - segment_len) {
            length = sizeof(stream->segment) - segment_len;
        }
        memcpy(stream->segment + segment_len,
               stream->span.data + stream->span_offset, length);
        stream->span_offset += length;
        stream->source_produced += length;
        stream->logical_offset += length;
        segment_len += length;
    }
    flags = stream->flags | ESP32_MQUICKJS_RPC_FLAG_STREAM;
    if (stream->logical_offset == segment_len) {
        flags |= ESP32_MQUICKJS_RPC_FLAG_FIRST;
    }
    if (stream->logical_offset == logical_len) {
        flags |= ESP32_MQUICKJS_RPC_FLAG_LAST;
    }
    if (esp32_mquickjs_rpc_wire_encode_segment(
            stream->opcode, stream->request_id, flags, (uint32_t)logical_len,
            (uint32_t)(stream->logical_offset - segment_len),
            (uint16_t)stream->prefix_len, stream->segment,
            (uint16_t)segment_len, stream->frame, sizeof(stream->frame),
            &wire_len) != ESP32_MQUICKJS_RPC_WIRE_OK) {
        JS_ThrowInternalError(ctx, "RPCCodec.encode() could not frame streamed data");
        return false;
    }
    out->data = stream->frame;
    out->length = wire_len;
    out->owner = JS_UNDEFINED;
    out->dma_capable = false;
    return true;
}

static void rpc_encoded_stream_close(JSContext *ctx, void *opaque)
{
    rpc_encoded_stream_object_t *stream = opaque;

    if (stream == NULL) {
        return;
    }
    if (stream->opened) {
        esp32_mquickjs_byte_span_source_close(ctx, &stream->source);
        stream->opened = false;
    }
    stream->consumed = true;
    if (stream->destroy_requested) {
        rpc_encoded_stream_free(stream);
    }
}

static bool rpc_encoded_stream_source_open(
    JSContext *ctx,
    JSValue source_value,
    void *opaque,
    esp32_mquickjs_byte_span_source_t *out,
    JSValue *out_error)
{
    if (!rpc_encoded_stream_open(ctx, source_value, opaque, out, out_error)) {
        return false;
    }
    out->next = rpc_encoded_stream_next;
    out->close = rpc_encoded_stream_close;
    return true;
}

static void rpc_encoded_stream_destroy(JSContext *ctx, void *opaque)
{
    rpc_encoded_stream_object_t *stream = opaque;

    if (stream == NULL) {
        return;
    }
    if (stream->opened) {
        stream->destroy_requested = true;
        return;
    }
    (void)ctx;
    rpc_encoded_stream_free(stream);
}

static const esp32_mquickjs_byte_span_source_object_ops_t s_rpc_encoded_stream_ops = {
    .class_id = JS_CLASS_BYTE_SPAN_SOURCE,
    .open = rpc_encoded_stream_source_open,
    .destroy = rpc_encoded_stream_destroy,
};

static JSValue rpc_make_encoded_stream(JSContext *ctx,
                                       uint16_t opcode,
                                       uint32_t request_id,
                                       uint8_t flags,
                                       rpc_buffer_t *payload)
{
    rpc_encoded_stream_object_t *stream = calloc(1, sizeof(*stream));
    JSValue *source;

    if (stream == NULL) {
        free(payload->data);
        return JS_ThrowOutOfMemory(ctx);
    }
    stream->ctx = ctx;
    stream->opcode = opcode;
    stream->request_id = request_id;
    stream->flags = flags;
    stream->prefix = payload->data;
    stream->prefix_len = payload->length;
    stream->source_len = payload->stream_length;
    esp32_mquickjs_byte_span_clear(&stream->span);
    source = JS_AddGCRef(ctx, &stream->source_ref);
    *source = payload->stream_value;
    stream->rooted = true;
    return esp32_mquickjs_new_byte_span_source(
        ctx, payload->stream_value, &s_rpc_encoded_stream_ops, stream);
}

JSValue js_rpc_encode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    rpc_codec_slot_t *codec;
    uint32_t opcode;
    uint32_t request_id;
    uint32_t flags;
    rpc_buffer_t payload;
    uint8_t *payload_data;
    JSGCRef frames_ref;
    JSValue *frames;
    size_t offset = 0;
    uint32_t index = 0;
    if (argc != 4 || this_val == NULL ||
        (codec = rpc_codec_from_value(
             ctx, *this_val, "RPCCodec.encode()")) == NULL ||
        JS_ToUint32(ctx, &opcode, argv[0]) != 0 || opcode == 0 ||
        opcode > UINT16_MAX || JS_ToUint32(ctx, &request_id, argv[1]) != 0 ||
        JS_ToUint32(ctx, &flags, argv[2]) != 0 ||
        (flags & ~(ESP32_MQUICKJS_RPC_FLAG_RESPONSE |
                   ESP32_MQUICKJS_RPC_FLAG_ERROR)) != 0) {
        return JS_ThrowTypeError(ctx,
                                 "RPCCodec.encode(opcode, requestId, flags, payload) received invalid arguments");
    }
    payload_data = malloc(ESP32_MQUICKJS_RPC_MESSAGE_BYTES);
    if (payload_data == NULL) return JS_ThrowOutOfMemory(ctx);
    payload = (rpc_buffer_t){
        .data = payload_data,
        .capacity = ESP32_MQUICKJS_RPC_MESSAGE_BYTES,
        .stream_value = JS_UNDEFINED,
    };
    if (!rpc_encode_value(ctx, codec, argv[3], &payload, 0,
                          codec->allow_string_keys) ||
        payload.failed) {
        free(payload_data);
        if (!JS_HasException(ctx)) {
            return JS_ThrowRangeError(ctx, "RPCCodec.encode() payload exceeds 65536 bytes");
        }
        return JS_EXCEPTION;
    }
    {
        CborParser parser;
        CborValue root;
        CborError validation_error;
        uint32_t validation_flags = CborValidateUtf8;

        if (!payload.has_stream || payload.stream_length == 0) {
            validation_flags |= (uint32_t)CborValidateCompleteData;
        }
        if (cbor_parser_init(payload.data, payload.length, 0, &parser, &root) !=
                CborNoError) {
            free(payload_data);
            return JS_ThrowInternalError(ctx,
                                         "RPCCodec.encode() produced invalid CBOR");
        }
        /* Delegate the CBOR text contract to the bundled codec. */
        validation_error = cbor_value_validate(&root, validation_flags);
        if ((!payload.has_stream || payload.stream_length == 0)
                ? validation_error != CborNoError
                : validation_error != CborErrorUnexpectedEOF) {
            free(payload_data);
            if (validation_error == CborErrorInvalidUtf8TextString) {
                return JS_ThrowTypeError(
                    ctx, "RPCCodec.encode() text values must be valid CBOR text");
            }
            return JS_ThrowInternalError(ctx,
                                         "RPCCodec.encode() produced invalid CBOR");
        }
    }
    if (payload.has_stream) {
        if (payload.length > ESP32_MQUICKJS_RPC_STREAM_PREFIX_BYTES ||
            payload.length + payload.stream_length > UINT32_MAX) {
            free(payload_data);
            return JS_ThrowRangeError(ctx, "RPCCodec.encode() stream exceeds its size limit");
        }
        return rpc_make_encoded_stream(ctx, (uint16_t)opcode, request_id,
                                       (uint8_t)flags, &payload);
    }
    frames = JS_PushGCRef(ctx, &frames_ref);
    *frames = JS_NewArray(ctx, 0);
    do {
        uint16_t segment_len = (uint16_t)((payload.length - offset) >
                ESP32_MQUICKJS_RPC_SEGMENT_BYTES
            ? ESP32_MQUICKJS_RPC_SEGMENT_BYTES : payload.length - offset);
        uint8_t segment_flags = (uint8_t)flags;
        uint8_t *wire = malloc(ESP32_MQUICKJS_RPC_MAX_FRAME_BYTES);
        size_t wire_len = 0;
        JSGCRef view_ref;
        JSValue *view;
        esp32_mquickjs_rpc_wire_error_t wire_error;
        if (wire == NULL) {
            free(payload_data);
            JS_PopGCRef(ctx, &frames_ref);
            return JS_ThrowOutOfMemory(ctx);
        }
        if (offset == 0) segment_flags |= ESP32_MQUICKJS_RPC_FLAG_FIRST;
        if (offset + segment_len == payload.length) segment_flags |= ESP32_MQUICKJS_RPC_FLAG_LAST;
        wire_error = esp32_mquickjs_rpc_wire_encode_segment(
            (uint16_t)opcode, request_id, segment_flags, (uint32_t)payload.length,
            (uint32_t)offset, 0, payload.data + offset, segment_len, wire,
            ESP32_MQUICKJS_RPC_MAX_FRAME_BYTES, &wire_len);
        if (wire_error != ESP32_MQUICKJS_RPC_WIRE_OK) {
            free(wire);
            free(payload_data);
            JS_PopGCRef(ctx, &frames_ref);
            return JS_ThrowInternalError(ctx, "RPCCodec.encode() wire error: %s",
                                         esp32_mquickjs_rpc_wire_error_name(wire_error));
        }
        view = JS_PushGCRef(ctx, &view_ref);
        *view = esp32_mquickjs_new_owned_byte_view(ctx, wire, wire_len);
        if (JS_IsException(*view) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *frames, index++, *view))) {
            JS_PopGCRef(ctx, &view_ref);
            free(payload_data);
            JS_PopGCRef(ctx, &frames_ref);
            return JS_EXCEPTION;
        }
        JS_PopGCRef(ctx, &view_ref);
        offset += segment_len;
    } while (offset < payload.length || (payload.length == 0 && index == 0));
    free(payload_data);
    return JS_PopGCRef(ctx, &frames_ref);
}

JSValue js_rpc_bytes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_byte_source_t source;
    uint8_t *owned = NULL;
    JSValue error = JS_UNDEFINED;
    uint8_t *copy;
    (void)this_val;
    if (argc != 1 || !esp32_mquickjs_get_byte_source(ctx, argv[0], "rpc.bytes()",
                                                     &source, &owned, &error)) {
        return JS_IsUndefined(error)
                   ? JS_ThrowTypeError(ctx, "rpc.bytes(value) expects byte data")
                   : error;
    }
    if (owned != NULL || source.length == 0) {
        return esp32_mquickjs_new_owned_byte_view(ctx, owned, source.length);
    }
    copy = malloc(source.length);
    if (copy == NULL) return JS_ThrowOutOfMemory(ctx);
    memcpy(copy, source.data, source.length);
    return esp32_mquickjs_new_owned_byte_view(ctx, copy, source.length);
}

JSValue js_rpc_file_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSCStringBuf path_buf;
    const char *path;
    struct stat info;

    (void)this_val;
    if (argc != 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "rpc.fileSource(path) expects a file path");
    }
    path = JS_ToCString(ctx, argv[0], &path_buf);
    if (path == NULL) {
        return JS_EXCEPTION;
    }
    if (path[0] == '\0' || stat(path, &info) != 0 ||
        !S_ISREG(info.st_mode) || info.st_size < 0 ||
        (uint64_t)info.st_size > ESP32_MQUICKJS_RPC_STREAM_BYTES) {
        return JS_ThrowTypeError(ctx,
                                 "rpc.fileSource(path) expects a readable regular file");
    }
    return rpc_new_file_source(ctx, path, (size_t)info.st_size, false, false, 0);
}

JSValue js_rpc_source_info(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    rpc_file_source_object_t *source;
    JSGCRef result_ref;
    JSValue *result;
    char crc[9];

    (void)this_val;
    if (argc != 1 ||
        (source = esp32_mquickjs_byte_span_source_get_opaque(
             ctx, argv[0], &s_rpc_file_source_ops, "rpc.sourceInfo(source)")) == NULL) {
        return JS_EXCEPTION;
    }
    result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (source->has_crc32) {
        snprintf(crc, sizeof(crc), "%08lx", (unsigned long)source->crc32);
    }
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "size", JS_NewUint32(ctx, (uint32_t)source->length)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "crc32",
            source->has_crc32 ? JS_NewString(ctx, crc) : JS_NULL)) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

JSValue js_rpc_adopt_file(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    rpc_file_source_object_t *source;
    JSCStringBuf path_buf;
    const char *path;

    (void)this_val;
    if (argc != 2 || !JS_IsString(ctx, argv[1]) ||
        (source = esp32_mquickjs_byte_span_source_get_opaque(
             ctx, argv[0], &s_rpc_file_source_ops, "rpc.adoptFile(source, path)")) == NULL) {
        return JS_EXCEPTION;
    }
    path = JS_ToCString(ctx, argv[1], &path_buf);
    if (path == NULL) {
        return JS_EXCEPTION;
    }
    if (!source->remove_on_destroy || source->opened || source->consumed ||
        source->adopted || source->path == NULL || path[0] == '\0') {
        return JS_ThrowTypeError(ctx, "rpc.adoptFile() expects an unused RPC upload source");
    }
    if (rename(source->path, path) != 0) {
        return JS_ThrowInternalError(ctx, "rpc.adoptFile() could not replace the target");
    }
    source->adopted = true;
    source->remove_on_destroy = false;
    esp32_mquickjs_fs_notify_change(
        ESP32_MQUICKJS_FS_CHANGE_WRITE, path, NULL);
    return JS_NewBool(true);
}

JSValue js_rpc_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    uint32_t active_codecs = 0;
    uint32_t active = 0;
    uint32_t messages = 0;
    uint32_t errors = 0;
    size_t i;
    (void)this_val; (void)argc; (void)argv;
    for (i = 0; i < RPC_DECODER_SLOTS; ++i) {
        if (s_rpc_decoders[i].used) active++;
        messages += s_rpc_decoders[i].messages;
        errors += s_rpc_decoders[i].errors;
    }
    for (i = 0; i < RPC_CODEC_SLOTS; ++i) {
        if (s_rpc_codecs[i].used) active_codecs++;
    }
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "protocol",
                                         JS_NewString(ctx, ESP32_MQUICKJS_RPC_PROTOCOL)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "activeCodecs",
                                         JS_NewUint32(ctx, active_codecs)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "activeDecoders",
                                         JS_NewUint32(ctx, active)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "messages", JS_NewUint32(ctx, messages)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "errors", JS_NewUint32(ctx, errors))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

void esp32_mquickjs_deinit_rpc_runtime(void)
{
    size_t i;

    for (i = 0; i < RPC_DECODER_SLOTS; ++i) {
        rpc_decoder_cleanup(&s_rpc_decoders[i]);
    }
    for (i = 0; i < RPC_CODEC_SLOTS; ++i) {
        if (s_rpc_codecs[i].used) {
            rpc_codec_cleanup(&s_rpc_codecs[i]);
        }
    }
}
