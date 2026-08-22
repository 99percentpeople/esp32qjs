#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP32_MQUICKJS_RPC_PROTOCOL "esp32qjs.rpc/1"
#define ESP32_MQUICKJS_RPC_VERSION 1U
#define ESP32_MQUICKJS_RPC_HEADER_BYTES 24U
#define ESP32_MQUICKJS_RPC_CRC_BYTES 4U
#define ESP32_MQUICKJS_RPC_SEGMENT_BYTES 7680U
#define ESP32_MQUICKJS_RPC_MESSAGE_BYTES 65536U
#define ESP32_MQUICKJS_RPC_STREAM_BYTES (32U * 1024U * 1024U)
#define ESP32_MQUICKJS_RPC_STREAM_PREFIX_BYTES 65535U
#define ESP32_MQUICKJS_RPC_REASSEMBLY_TIMEOUT_MS 30000U
#define ESP32_MQUICKJS_RPC_MAX_RECORD_BYTES \
    (ESP32_MQUICKJS_RPC_HEADER_BYTES + ESP32_MQUICKJS_RPC_SEGMENT_BYTES + \
     ESP32_MQUICKJS_RPC_CRC_BYTES)
#define ESP32_MQUICKJS_RPC_MAX_FRAME_BYTES \
    (ESP32_MQUICKJS_RPC_MAX_RECORD_BYTES + \
     (ESP32_MQUICKJS_RPC_MAX_RECORD_BYTES / 254U) + 2U)

#define ESP32_MQUICKJS_RPC_FLAG_FIRST 0x01U
#define ESP32_MQUICKJS_RPC_FLAG_LAST 0x02U
#define ESP32_MQUICKJS_RPC_FLAG_RESPONSE 0x04U
#define ESP32_MQUICKJS_RPC_FLAG_ERROR 0x08U
#define ESP32_MQUICKJS_RPC_FLAG_STREAM 0x10U
#define ESP32_MQUICKJS_RPC_FLAG_MASK 0x1fU

typedef enum {
    ESP32_MQUICKJS_RPC_WIRE_OK = 0,
    ESP32_MQUICKJS_RPC_WIRE_INVALID_ARGUMENT,
    ESP32_MQUICKJS_RPC_WIRE_OUT_OF_MEMORY,
    ESP32_MQUICKJS_RPC_WIRE_FRAME_TOO_LARGE,
    ESP32_MQUICKJS_RPC_WIRE_COBS_INVALID,
    ESP32_MQUICKJS_RPC_WIRE_HEADER_INVALID,
    ESP32_MQUICKJS_RPC_WIRE_CRC_INVALID,
    ESP32_MQUICKJS_RPC_WIRE_SEGMENT_INVALID,
    ESP32_MQUICKJS_RPC_WIRE_INTERLEAVED,
    ESP32_MQUICKJS_RPC_WIRE_TIMEOUT,
    ESP32_MQUICKJS_RPC_WIRE_STREAM_FAILED,
} esp32_mquickjs_rpc_wire_error_t;

typedef struct {
    uint8_t flags;
    uint16_t opcode;
    uint32_t request_id;
    const uint8_t *payload;
    size_t payload_len;
    size_t logical_len;
    size_t stream_len;
} esp32_mquickjs_rpc_wire_message_t;

typedef struct {
    uint8_t flags;
    uint16_t opcode;
    uint32_t request_id;
    uint32_t logical_len;
    uint32_t stream_len;
    uint32_t offset;
    const uint8_t *data;
    size_t data_len;
    bool first;
    bool last;
} esp32_mquickjs_rpc_wire_stream_chunk_t;

typedef struct {
    uint8_t frame[ESP32_MQUICKJS_RPC_MAX_FRAME_BYTES];
    size_t frame_len;
    bool discarding;
    uint8_t *reassembly;
    size_t logical_len;
    size_t next_offset;
    size_t prefix_len;
    uint16_t opcode;
    uint32_t request_id;
    uint8_t flags;
    uint64_t started_ms;
} esp32_mquickjs_rpc_wire_decoder_t;

typedef void (*esp32_mquickjs_rpc_wire_message_fn)(
    void *opaque,
    const esp32_mquickjs_rpc_wire_message_t *message);
typedef void (*esp32_mquickjs_rpc_wire_error_fn)(
    void *opaque,
    esp32_mquickjs_rpc_wire_error_t error);
typedef bool (*esp32_mquickjs_rpc_wire_stream_fn)(
    void *opaque,
    const esp32_mquickjs_rpc_wire_stream_chunk_t *chunk);

uint32_t esp32_mquickjs_rpc_crc32(const uint8_t *data, size_t length);
uint32_t esp32_mquickjs_rpc_crc32_update(uint32_t state,
                                         const uint8_t *data,
                                         size_t length);

size_t esp32_mquickjs_rpc_cobs_encode(const uint8_t *input,
                                      size_t input_len,
                                      uint8_t *output,
                                      size_t output_capacity);
size_t esp32_mquickjs_rpc_cobs_decode(const uint8_t *input,
                                      size_t input_len,
                                      uint8_t *output,
                                      size_t output_capacity);

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
    size_t *out_length);

void esp32_mquickjs_rpc_wire_decoder_init(
    esp32_mquickjs_rpc_wire_decoder_t *decoder);
void esp32_mquickjs_rpc_wire_decoder_reset(
    esp32_mquickjs_rpc_wire_decoder_t *decoder);
size_t esp32_mquickjs_rpc_wire_decoder_feed(
    esp32_mquickjs_rpc_wire_decoder_t *decoder,
    const uint8_t *data,
    size_t data_len,
    uint64_t now_ms,
    esp32_mquickjs_rpc_wire_message_fn on_message,
    esp32_mquickjs_rpc_wire_stream_fn on_stream,
    esp32_mquickjs_rpc_wire_error_fn on_error,
    void *opaque);

const char *esp32_mquickjs_rpc_wire_error_name(
    esp32_mquickjs_rpc_wire_error_t error);
