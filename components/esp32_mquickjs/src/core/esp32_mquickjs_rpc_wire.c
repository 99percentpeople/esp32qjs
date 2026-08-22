#include "esp32qjs_rpc_wire.h"

#include <stdlib.h>
#include <string.h>

static uint16_t read_u16be(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static uint32_t read_u32be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | (uint32_t)data[3];
}

static void write_u16be(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static void write_u32be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

uint32_t esp32_mquickjs_rpc_crc32(const uint8_t *data, size_t length)
{
    return esp32_mquickjs_rpc_crc32_update(UINT32_C(0xffffffff), data, length) ^
           UINT32_C(0xffffffff);
}

uint32_t esp32_mquickjs_rpc_crc32_update(uint32_t state,
                                         const uint8_t *data,
                                         size_t length)
{
    uint32_t crc = state;
    size_t i;
    unsigned int bit;

    if (data == NULL && length != 0) {
        return 0;
    }
    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0
                      ? UINT32_C(0xedb88320) ^ (crc >> 1U)
                      : crc >> 1U;
        }
    }
    return crc;
}

size_t esp32_mquickjs_rpc_cobs_encode(const uint8_t *input,
                                      size_t input_len,
                                      uint8_t *output,
                                      size_t output_capacity)
{
    size_t read_index = 0;
    size_t write_index = 1;
    size_t code_index = 0;
    uint8_t code = 1;

    if ((input == NULL && input_len != 0) || output == NULL || output_capacity == 0) {
        return 0;
    }
    while (read_index < input_len) {
        if (input[read_index] == 0) {
            if (code_index >= output_capacity) {
                return 0;
            }
            output[code_index] = code;
            code = 1;
            code_index = write_index++;
            if (write_index > output_capacity) {
                return 0;
            }
            read_index++;
        } else {
            if (write_index >= output_capacity) {
                return 0;
            }
            output[write_index++] = input[read_index++];
            code++;
            if (code == 0xffU) {
                if (code_index >= output_capacity) {
                    return 0;
                }
                output[code_index] = code;
                code = 1;
                code_index = write_index++;
                if (write_index > output_capacity) {
                    return 0;
                }
            }
        }
    }
    if (code_index >= output_capacity) {
        return 0;
    }
    output[code_index] = code;
    return write_index;
}

size_t esp32_mquickjs_rpc_cobs_decode(const uint8_t *input,
                                      size_t input_len,
                                      uint8_t *output,
                                      size_t output_capacity)
{
    size_t read_index = 0;
    size_t write_index = 0;

    if (input == NULL || input_len == 0 || output == NULL) {
        return 0;
    }
    while (read_index < input_len) {
        uint8_t code = input[read_index++];
        size_t copy_len;

        if (code == 0) {
            return 0;
        }
        copy_len = (size_t)code - 1U;
        if (copy_len > input_len - read_index ||
            copy_len > output_capacity - write_index) {
            return 0;
        }
        if (copy_len != 0) {
            memmove(output + write_index, input + read_index, copy_len);
            read_index += copy_len;
            write_index += copy_len;
        }
        if (code != 0xffU && read_index < input_len) {
            if (write_index >= output_capacity) {
                return 0;
            }
            output[write_index++] = 0;
        }
    }
    return write_index;
}

static bool flags_valid(uint8_t flags,
                        uint32_t logical_length,
                        uint32_t offset,
                        uint16_t segment_length,
                        uint16_t stream_prefix_length)
{
    bool stream = (flags & ESP32_MQUICKJS_RPC_FLAG_STREAM) != 0;
    uint32_t maximum = stream
        ? ESP32_MQUICKJS_RPC_STREAM_PREFIX_BYTES + ESP32_MQUICKJS_RPC_STREAM_BYTES
        : ESP32_MQUICKJS_RPC_MESSAGE_BYTES;

    if ((flags & ~ESP32_MQUICKJS_RPC_FLAG_MASK) != 0 ||
        ((flags & ESP32_MQUICKJS_RPC_FLAG_ERROR) != 0 &&
         (flags & ESP32_MQUICKJS_RPC_FLAG_RESPONSE) == 0) ||
        logical_length > maximum ||
        segment_length > ESP32_MQUICKJS_RPC_SEGMENT_BYTES ||
        offset > logical_length || segment_length > logical_length - offset) {
        return false;
    }
    if ((stream && (stream_prefix_length == 0 ||
                    stream_prefix_length > logical_length ||
                    logical_length - stream_prefix_length > ESP32_MQUICKJS_RPC_STREAM_BYTES)) ||
        (!stream && stream_prefix_length != 0)) {
        return false;
    }
    if (((flags & ESP32_MQUICKJS_RPC_FLAG_FIRST) != 0) != (offset == 0) ||
        ((flags & ESP32_MQUICKJS_RPC_FLAG_LAST) != 0) !=
            (offset + segment_length == logical_length)) {
        return false;
    }
    return logical_length != 0 ||
           (offset == 0 && segment_length == 0 &&
            (flags & (ESP32_MQUICKJS_RPC_FLAG_FIRST |
                      ESP32_MQUICKJS_RPC_FLAG_LAST)) ==
                (ESP32_MQUICKJS_RPC_FLAG_FIRST |
                 ESP32_MQUICKJS_RPC_FLAG_LAST));
}

esp32_mquickjs_rpc_wire_error_t esp32_mquickjs_rpc_wire_encode_segment(
    uint16_t opcode,
    uint32_t request_id,
    uint8_t flags,
    uint32_t logical_length,
    uint32_t offset,
    uint16_t stream_prefix_length,
    const uint8_t *payload,
    uint16_t payload_len,
    uint8_t *output,
    size_t output_capacity,
    size_t *out_length)
{
    uint8_t *record;
    size_t record_len = ESP32_MQUICKJS_RPC_HEADER_BYTES + payload_len +
                        ESP32_MQUICKJS_RPC_CRC_BYTES;
    size_t encoded_len;
    uint32_t crc;

    if (opcode == 0 || (payload == NULL && payload_len != 0) || output == NULL ||
        out_length == NULL ||
        !flags_valid(flags, logical_length, offset, payload_len,
                     stream_prefix_length)) {
        return ESP32_MQUICKJS_RPC_WIRE_INVALID_ARGUMENT;
    }
    record = malloc(record_len == 0 ? 1U : record_len);
    if (record == NULL) {
        return ESP32_MQUICKJS_RPC_WIRE_OUT_OF_MEMORY;
    }
    memcpy(record, "EQRP", 4);
    record[4] = ESP32_MQUICKJS_RPC_VERSION;
    record[5] = flags;
    write_u16be(record + 6, opcode);
    write_u32be(record + 8, request_id);
    write_u32be(record + 12, logical_length);
    write_u32be(record + 16, offset);
    write_u16be(record + 20, payload_len);
    write_u16be(record + 22, stream_prefix_length);
    if (payload_len != 0) {
        memcpy(record + ESP32_MQUICKJS_RPC_HEADER_BYTES, payload, payload_len);
    }
    crc = esp32_mquickjs_rpc_crc32(record,
                                  ESP32_MQUICKJS_RPC_HEADER_BYTES + payload_len);
    write_u32be(record + ESP32_MQUICKJS_RPC_HEADER_BYTES + payload_len, crc);
    if (output_capacity < 2) {
        free(record);
        return ESP32_MQUICKJS_RPC_WIRE_FRAME_TOO_LARGE;
    }
    encoded_len = esp32_mquickjs_rpc_cobs_encode(record, record_len, output,
                                                 output_capacity - 1U);
    free(record);
    if (encoded_len == 0 || encoded_len >= output_capacity) {
        return ESP32_MQUICKJS_RPC_WIRE_FRAME_TOO_LARGE;
    }
    output[encoded_len] = 0;
    *out_length = encoded_len + 1U;
    return ESP32_MQUICKJS_RPC_WIRE_OK;
}

void esp32_mquickjs_rpc_wire_decoder_init(
    esp32_mquickjs_rpc_wire_decoder_t *decoder)
{
    if (decoder != NULL) {
        memset(decoder, 0, sizeof(*decoder));
    }
}

void esp32_mquickjs_rpc_wire_decoder_reset(
    esp32_mquickjs_rpc_wire_decoder_t *decoder)
{
    if (decoder == NULL) {
        return;
    }
    free(decoder->reassembly);
    memset(decoder, 0, sizeof(*decoder));
}

static void report_error(esp32_mquickjs_rpc_wire_error_fn callback,
                         void *opaque,
                         esp32_mquickjs_rpc_wire_error_t error)
{
    if (callback != NULL) {
        callback(opaque, error);
    }
}

static esp32_mquickjs_rpc_wire_error_t decode_record(
    esp32_mquickjs_rpc_wire_decoder_t *decoder,
    uint64_t now_ms,
    esp32_mquickjs_rpc_wire_message_fn on_message,
    esp32_mquickjs_rpc_wire_stream_fn on_stream,
    void *opaque,
    bool *emitted)
{
    uint8_t *record = decoder->frame;
    size_t record_len;
    uint8_t flags;
    uint16_t opcode;
    uint32_t request_id;
    uint32_t logical_length;
    uint32_t offset;
    uint16_t segment_length;
    uint16_t stream_prefix_length;
    uint32_t expected_crc;
    uint32_t actual_crc;
    const uint8_t *segment;

    *emitted = false;
    record_len = esp32_mquickjs_rpc_cobs_decode(decoder->frame,
                                                decoder->frame_len,
                                                record,
                                                sizeof(decoder->frame));
    if (record_len < ESP32_MQUICKJS_RPC_HEADER_BYTES +
                         ESP32_MQUICKJS_RPC_CRC_BYTES) {
        return ESP32_MQUICKJS_RPC_WIRE_COBS_INVALID;
    }
    flags = record[5];
    opcode = read_u16be(record + 6);
    request_id = read_u32be(record + 8);
    logical_length = read_u32be(record + 12);
    offset = read_u32be(record + 16);
    segment_length = read_u16be(record + 20);
    stream_prefix_length = read_u16be(record + 22);
    if (memcmp(record, "EQRP", 4) != 0 ||
        record[4] != ESP32_MQUICKJS_RPC_VERSION || opcode == 0 ||
        record_len != ESP32_MQUICKJS_RPC_HEADER_BYTES + segment_length +
                          ESP32_MQUICKJS_RPC_CRC_BYTES ||
        !flags_valid(flags, logical_length, offset, segment_length,
                     stream_prefix_length)) {
        return ESP32_MQUICKJS_RPC_WIRE_HEADER_INVALID;
    }
    expected_crc = read_u32be(record + record_len - ESP32_MQUICKJS_RPC_CRC_BYTES);
    actual_crc = esp32_mquickjs_rpc_crc32(record,
                                         record_len - ESP32_MQUICKJS_RPC_CRC_BYTES);
    if (expected_crc != actual_crc) {
        return ESP32_MQUICKJS_RPC_WIRE_CRC_INVALID;
    }
    segment = record + ESP32_MQUICKJS_RPC_HEADER_BYTES;

    if ((flags & ESP32_MQUICKJS_RPC_FLAG_FIRST) != 0) {
        if (decoder->reassembly != NULL) {
            return ESP32_MQUICKJS_RPC_WIRE_INTERLEAVED;
        }
        size_t stored_length =
            (flags & ESP32_MQUICKJS_RPC_FLAG_STREAM) != 0
                ? stream_prefix_length
                : logical_length;
        decoder->reassembly = stored_length == 0 ? malloc(1) : malloc(stored_length);
        if (decoder->reassembly == NULL) {
            return ESP32_MQUICKJS_RPC_WIRE_OUT_OF_MEMORY;
        }
        decoder->logical_len = logical_length;
        decoder->prefix_len = stream_prefix_length;
        decoder->next_offset = 0;
        decoder->opcode = opcode;
        decoder->request_id = request_id;
        decoder->flags = flags & (ESP32_MQUICKJS_RPC_FLAG_RESPONSE |
                                  ESP32_MQUICKJS_RPC_FLAG_ERROR |
                                  ESP32_MQUICKJS_RPC_FLAG_STREAM);
        decoder->started_ms = now_ms;
    } else if (decoder->reassembly == NULL) {
        return ESP32_MQUICKJS_RPC_WIRE_SEGMENT_INVALID;
    }

    if (decoder->opcode != opcode || decoder->request_id != request_id ||
        decoder->logical_len != logical_length || decoder->next_offset != offset ||
        decoder->prefix_len != stream_prefix_length ||
        decoder->flags != (flags & (ESP32_MQUICKJS_RPC_FLAG_RESPONSE |
                                    ESP32_MQUICKJS_RPC_FLAG_ERROR |
                                    ESP32_MQUICKJS_RPC_FLAG_STREAM))) {
        esp32_mquickjs_rpc_wire_decoder_reset(decoder);
        return ESP32_MQUICKJS_RPC_WIRE_INTERLEAVED;
    }
    if ((decoder->flags & ESP32_MQUICKJS_RPC_FLAG_STREAM) != 0) {
        size_t prefix_part = 0;
        size_t stream_part;
        if (offset < decoder->prefix_len) {
            prefix_part = decoder->prefix_len - offset;
            if (prefix_part > segment_length) {
                prefix_part = segment_length;
            }
            if (prefix_part != 0) {
                memcpy(decoder->reassembly + offset, segment, prefix_part);
            }
        }
        stream_part = segment_length - prefix_part;
        if (stream_part != 0) {
            esp32_mquickjs_rpc_wire_stream_chunk_t chunk = {
                .flags = decoder->flags,
                .opcode = decoder->opcode,
                .request_id = decoder->request_id,
                .logical_len = (uint32_t)decoder->logical_len,
                .stream_len = (uint32_t)(decoder->logical_len - decoder->prefix_len),
                .offset = (uint32_t)(offset + prefix_part - decoder->prefix_len),
                .data = segment + prefix_part,
                .data_len = stream_part,
                .first = offset + prefix_part == decoder->prefix_len,
                .last = offset + segment_length == decoder->logical_len,
            };
            if (on_stream == NULL || !on_stream(opaque, &chunk)) {
                esp32_mquickjs_rpc_wire_decoder_reset(decoder);
                return ESP32_MQUICKJS_RPC_WIRE_STREAM_FAILED;
            }
        }
    } else if (segment_length != 0) {
        memcpy(decoder->reassembly + offset, segment, segment_length);
    }
    decoder->next_offset += segment_length;
    decoder->started_ms = now_ms;
    if ((flags & ESP32_MQUICKJS_RPC_FLAG_LAST) != 0) {
        esp32_mquickjs_rpc_wire_message_t message = {
            .flags = (uint8_t)(decoder->flags |
                               ESP32_MQUICKJS_RPC_FLAG_FIRST |
                               ESP32_MQUICKJS_RPC_FLAG_LAST),
            .opcode = decoder->opcode,
            .request_id = decoder->request_id,
            .payload = decoder->reassembly,
            .payload_len = (decoder->flags & ESP32_MQUICKJS_RPC_FLAG_STREAM) != 0
                ? decoder->prefix_len : decoder->logical_len,
            .logical_len = decoder->logical_len,
            .stream_len = decoder->logical_len - decoder->prefix_len,
        };
        if (on_message != NULL) {
            on_message(opaque, &message);
        }
        free(decoder->reassembly);
        decoder->reassembly = NULL;
        decoder->logical_len = 0;
        decoder->next_offset = 0;
        decoder->prefix_len = 0;
        *emitted = true;
    }
    return ESP32_MQUICKJS_RPC_WIRE_OK;
}

size_t esp32_mquickjs_rpc_wire_decoder_feed(
    esp32_mquickjs_rpc_wire_decoder_t *decoder,
    const uint8_t *data,
    size_t data_len,
    uint64_t now_ms,
    esp32_mquickjs_rpc_wire_message_fn on_message,
    esp32_mquickjs_rpc_wire_stream_fn on_stream,
    esp32_mquickjs_rpc_wire_error_fn on_error,
    void *opaque)
{
    size_t emitted = 0;
    size_t i;

    if (decoder == NULL || (data == NULL && data_len != 0)) {
        report_error(on_error, opaque, ESP32_MQUICKJS_RPC_WIRE_INVALID_ARGUMENT);
        return 0;
    }
    if (decoder->reassembly != NULL &&
        now_ms - decoder->started_ms >= ESP32_MQUICKJS_RPC_REASSEMBLY_TIMEOUT_MS) {
        free(decoder->reassembly);
        decoder->reassembly = NULL;
        decoder->logical_len = 0;
        decoder->next_offset = 0;
        report_error(on_error, opaque, ESP32_MQUICKJS_RPC_WIRE_TIMEOUT);
    }
    for (i = 0; i < data_len; ++i) {
        if (data[i] == 0) {
            if (decoder->discarding) {
                decoder->discarding = false;
                decoder->frame_len = 0;
                continue;
            }
            if (decoder->frame_len != 0) {
                bool did_emit;
                esp32_mquickjs_rpc_wire_error_t error =
                    decode_record(decoder, now_ms, on_message, on_stream,
                                  opaque, &did_emit);
                if (error != ESP32_MQUICKJS_RPC_WIRE_OK) {
                    report_error(on_error, opaque, error);
                } else if (did_emit) {
                    emitted++;
                }
            }
            decoder->frame_len = 0;
            continue;
        }
        if (decoder->discarding) {
            continue;
        }
        if (decoder->frame_len >= sizeof(decoder->frame)) {
            decoder->frame_len = 0;
            decoder->discarding = true;
            report_error(on_error, opaque, ESP32_MQUICKJS_RPC_WIRE_FRAME_TOO_LARGE);
            continue;
        }
        decoder->frame[decoder->frame_len++] = data[i];
    }
    return emitted;
}

const char *esp32_mquickjs_rpc_wire_error_name(esp32_mquickjs_rpc_wire_error_t error)
{
    switch (error) {
    case ESP32_MQUICKJS_RPC_WIRE_OK: return "OK";
    case ESP32_MQUICKJS_RPC_WIRE_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case ESP32_MQUICKJS_RPC_WIRE_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    case ESP32_MQUICKJS_RPC_WIRE_FRAME_TOO_LARGE: return "FRAME_TOO_LARGE";
    case ESP32_MQUICKJS_RPC_WIRE_COBS_INVALID: return "COBS_INVALID";
    case ESP32_MQUICKJS_RPC_WIRE_HEADER_INVALID: return "HEADER_INVALID";
    case ESP32_MQUICKJS_RPC_WIRE_CRC_INVALID: return "CRC_INVALID";
    case ESP32_MQUICKJS_RPC_WIRE_SEGMENT_INVALID: return "SEGMENT_INVALID";
    case ESP32_MQUICKJS_RPC_WIRE_INTERLEAVED: return "INTERLEAVED";
    case ESP32_MQUICKJS_RPC_WIRE_TIMEOUT: return "TIMEOUT";
    case ESP32_MQUICKJS_RPC_WIRE_STREAM_FAILED: return "STREAM_FAILED";
    }
    return "UNKNOWN";
}
