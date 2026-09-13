#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp32qjs_rpc_wire.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct {
    uint8_t payload[ESP32_MQUICKJS_RPC_MESSAGE_BYTES];
    size_t payload_len;
    uint16_t opcode;
    uint32_t request_id;
    uint8_t flags;
    uint8_t stream[10000];
    size_t stream_len;
    size_t logical_len;
    size_t messages;
    esp32_mquickjs_rpc_wire_error_t errors[16];
    size_t error_count;
} capture_t;

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

static size_t load_golden_hex(const char *name, uint8_t *output, size_t capacity)
{
    FILE *file = fopen(RPC_GOLDEN_VECTOR_PATH, "r");
    char line[ESP32_MQUICKJS_RPC_MAX_FRAME_BYTES * 2U + 32U];
    size_t name_len = strlen(name);
    size_t result = 0;

    if (file == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *hex;
        size_t length;
        size_t index;
        if (strncmp(line, name, name_len) != 0 || line[name_len] != '=') {
            continue;
        }
        hex = line + name_len + 1U;
        length = strcspn(hex, "\r\n");
        if ((length & 1U) != 0U || length / 2U > capacity) {
            break;
        }
        for (index = 0; index < length; index += 2U) {
            int high = hex_nibble(hex[index]);
            int low = hex_nibble(hex[index + 1U]);
            if (high < 0 || low < 0) {
                result = 0;
                goto done;
            }
            output[index / 2U] = (uint8_t)((high << 4) | low);
        }
        result = length / 2U;
        break;
    }
done:
    fclose(file);
    return result;
}

static void capture_message(void *opaque,
                            const esp32_mquickjs_rpc_wire_message_t *message)
{
    capture_t *capture = opaque;
    capture->payload_len = message->payload_len;
    memcpy(capture->payload, message->payload, message->payload_len);
    capture->opcode = message->opcode;
    capture->request_id = message->request_id;
    capture->flags = message->flags;
    capture->logical_len = message->logical_len;
    capture->messages++;
}

static bool capture_stream(void *opaque,
                           const esp32_mquickjs_rpc_wire_stream_chunk_t *chunk)
{
    capture_t *capture = opaque;
    if (chunk->offset != capture->stream_len ||
        chunk->data_len > sizeof(capture->stream) - capture->stream_len) {
        return false;
    }
    memcpy(capture->stream + capture->stream_len, chunk->data, chunk->data_len);
    capture->stream_len += chunk->data_len;
    return true;
}

static void capture_error(void *opaque, esp32_mquickjs_rpc_wire_error_t error)
{
    capture_t *capture = opaque;
    if (capture->error_count < sizeof(capture->errors) / sizeof(capture->errors[0])) {
        capture->errors[capture->error_count++] = error;
    }
}

static int test_crc_and_cobs_all_bytes(void)
{
    uint8_t source[1024];
    uint8_t encoded[1100];
    uint8_t decoded[1024];
    size_t encoded_len;
    size_t decoded_len;
    size_t i;

    CHECK(esp32_mquickjs_rpc_crc32((const uint8_t *)"123456789", 9) ==
          UINT32_C(0xcbf43926));
    for (i = 0; i < sizeof(source); ++i) {
        source[i] = (uint8_t)i;
    }
    encoded_len = esp32_mquickjs_rpc_cobs_encode(source, sizeof(source),
                                                 encoded, sizeof(encoded));
    CHECK(encoded_len > sizeof(source));
    CHECK(memchr(encoded, 0, encoded_len) == NULL);
    decoded_len = esp32_mquickjs_rpc_cobs_decode(encoded, encoded_len,
                                                 decoded, sizeof(decoded));
    CHECK(decoded_len == sizeof(source));
    CHECK(memcmp(decoded, source, sizeof(source)) == 0);
    decoded_len = esp32_mquickjs_rpc_cobs_decode(encoded, encoded_len,
                                                 encoded, sizeof(encoded));
    CHECK(decoded_len == sizeof(source));
    CHECK(memcmp(encoded, source, sizeof(source)) == 0);
    return 0;
}

static int test_shared_golden_vector(void)
{
    uint8_t cbor[64];
    uint8_t expected[128];
    uint8_t actual[128];
    size_t cbor_len = load_golden_hex("cbor", cbor, sizeof(cbor));
    size_t expected_len = load_golden_hex("frame", expected, sizeof(expected));
    size_t actual_len = 0;
    const uint8_t tool_call[] = {0xa1, 0x00, 0x64, 0x72, 0x65, 0x61, 0x64};

    CHECK(cbor_len == 25U);
    CHECK(cbor[0] == 0xa3U && cbor[6] == 0x43U && cbor[cbor_len - 9U] == 0xfbU);
    CHECK(expected_len > 0U);
    CHECK(esp32_mquickjs_rpc_wire_encode_segment(
              0x0101, UINT32_C(0x01020304),
              ESP32_MQUICKJS_RPC_FLAG_FIRST | ESP32_MQUICKJS_RPC_FLAG_LAST,
              sizeof(tool_call), 0, 0, tool_call, sizeof(tool_call), actual,
              sizeof(actual), &actual_len) == ESP32_MQUICKJS_RPC_WIRE_OK);
    CHECK(actual_len == expected_len);
    CHECK(memcmp(actual, expected, expected_len) == 0);
    return 0;
}

static int test_arbitrary_slices_and_noise_resync(void)
{
    esp32_mquickjs_rpc_wire_decoder_t decoder;
    capture_t capture = {0};
    uint8_t wire[ESP32_MQUICKJS_RPC_MAX_FRAME_BYTES];
    const uint8_t payload[] = {0xa2, 0x00, 0xf5, 0x01, 0x43, 0x00, 0x7f, 0xff};
    const uint8_t noise[] = {0x55, 0xaa, 0x13, 0x00};
    size_t wire_len = 0;
    size_t offset = 0;
    size_t slice = 1;

    CHECK(esp32_mquickjs_rpc_wire_encode_segment(
              0x0101, 7,
              ESP32_MQUICKJS_RPC_FLAG_FIRST | ESP32_MQUICKJS_RPC_FLAG_LAST,
              sizeof(payload), 0, 0, payload, sizeof(payload), wire,
              sizeof(wire), &wire_len) == ESP32_MQUICKJS_RPC_WIRE_OK);
    esp32_mquickjs_rpc_wire_decoder_init(&decoder);
    esp32_mquickjs_rpc_wire_decoder_feed(&decoder, noise, sizeof(noise), 0,
                                         capture_message, NULL,
                                         capture_error, &capture);
    while (offset < wire_len) {
        size_t length = slice;
        if (length > wire_len - offset) {
            length = wire_len - offset;
        }
        esp32_mquickjs_rpc_wire_decoder_feed(&decoder, wire + offset, length, 1,
                                             capture_message, NULL,
                                             capture_error, &capture);
        offset += length;
        slice = slice == 17 ? 1 : slice + 1;
    }
    CHECK(capture.messages == 1);
    CHECK(capture.opcode == 0x0101);
    CHECK(capture.request_id == 7);
    CHECK(capture.payload_len == sizeof(payload));
    CHECK(memcmp(capture.payload, payload, sizeof(payload)) == 0);
    CHECK(capture.error_count == 1);
    esp32_mquickjs_rpc_wire_decoder_reset(&decoder);
    return 0;
}

static int test_segmentation_crc_interleave_and_timeout(void)
{
    esp32_mquickjs_rpc_wire_decoder_t decoder;
    capture_t capture = {0};
    uint8_t first[ESP32_MQUICKJS_RPC_MAX_FRAME_BYTES];
    uint8_t last[ESP32_MQUICKJS_RPC_MAX_FRAME_BYTES];
    uint8_t corrupt[ESP32_MQUICKJS_RPC_MAX_FRAME_BYTES];
    uint8_t payload[4097];
    size_t first_len = 0;
    size_t last_len = 0;
    size_t i;

    for (i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(i * 31U);
    }
    CHECK(esp32_mquickjs_rpc_wire_encode_segment(
              0x0101, 99, ESP32_MQUICKJS_RPC_FLAG_FIRST,
              sizeof(payload), 0, 0, payload, 4096, first, sizeof(first),
              &first_len) == ESP32_MQUICKJS_RPC_WIRE_OK);
    CHECK(esp32_mquickjs_rpc_wire_encode_segment(
              0x0101, 99, ESP32_MQUICKJS_RPC_FLAG_LAST,
              sizeof(payload), 4096, 0, payload + 4096, 1, last, sizeof(last),
              &last_len) == ESP32_MQUICKJS_RPC_WIRE_OK);

    esp32_mquickjs_rpc_wire_decoder_init(&decoder);
    esp32_mquickjs_rpc_wire_decoder_feed(&decoder, first, first_len, 10,
                                         capture_message, NULL,
                                         capture_error, &capture);
    CHECK(capture.messages == 0);
    esp32_mquickjs_rpc_wire_decoder_feed(&decoder, last, last_len, 11,
                                         capture_message, NULL,
                                         capture_error, &capture);
    CHECK(capture.messages == 1);
    CHECK(capture.payload_len == sizeof(payload));
    CHECK(memcmp(capture.payload, payload, sizeof(payload)) == 0);

    memcpy(corrupt, first, first_len);
    corrupt[first_len / 2] ^= 0x40;
    esp32_mquickjs_rpc_wire_decoder_feed(&decoder, corrupt, first_len, 20,
                                         capture_message, NULL,
                                         capture_error, &capture);
    CHECK(capture.errors[capture.error_count - 1] ==
          ESP32_MQUICKJS_RPC_WIRE_CRC_INVALID);

    esp32_mquickjs_rpc_wire_decoder_feed(&decoder, first, first_len, 30,
                                         capture_message, NULL,
                                         capture_error, &capture);
    esp32_mquickjs_rpc_wire_decoder_feed(&decoder, first, first_len, 31,
                                         capture_message, NULL,
                                         capture_error, &capture);
    CHECK(capture.errors[capture.error_count - 1] ==
          ESP32_MQUICKJS_RPC_WIRE_INTERLEAVED);

    esp32_mquickjs_rpc_wire_decoder_feed(&decoder, first, first_len, 40,
                                         capture_message, NULL,
                                         capture_error, &capture);
    esp32_mquickjs_rpc_wire_decoder_feed(&decoder, NULL, 0,
                                         40 + ESP32_MQUICKJS_RPC_REASSEMBLY_TIMEOUT_MS,
                                         capture_message, NULL,
                                         capture_error, &capture);
    CHECK(capture.errors[capture.error_count - 1] ==
          ESP32_MQUICKJS_RPC_WIRE_TIMEOUT);
    esp32_mquickjs_rpc_wire_decoder_reset(&decoder);
    return 0;
}

static int test_transparent_stream(void)
{
    esp32_mquickjs_rpc_wire_decoder_t decoder;
    capture_t capture = {0};
    uint8_t logical[8014];
    uint8_t first[ESP32_MQUICKJS_RPC_MAX_FRAME_BYTES];
    uint8_t last[ESP32_MQUICKJS_RPC_MAX_FRAME_BYTES];
    size_t first_len = 0;
    size_t last_len = 0;
    size_t i;
    const uint16_t prefix_len = 13;

    for (i = 0; i < sizeof(logical); ++i) {
        logical[i] = i < prefix_len ? (uint8_t)(0xa0U + i) : (uint8_t)(i * 73U);
    }
    CHECK(esp32_mquickjs_rpc_wire_encode_segment(
              0x0101, 123,
              ESP32_MQUICKJS_RPC_FLAG_STREAM | ESP32_MQUICKJS_RPC_FLAG_FIRST,
              sizeof(logical), 0, prefix_len, logical,
              ESP32_MQUICKJS_RPC_SEGMENT_BYTES, first, sizeof(first),
              &first_len) == ESP32_MQUICKJS_RPC_WIRE_OK);
    CHECK(esp32_mquickjs_rpc_wire_encode_segment(
              0x0101, 123,
              ESP32_MQUICKJS_RPC_FLAG_STREAM | ESP32_MQUICKJS_RPC_FLAG_LAST,
              sizeof(logical), ESP32_MQUICKJS_RPC_SEGMENT_BYTES, prefix_len,
              logical + ESP32_MQUICKJS_RPC_SEGMENT_BYTES,
              sizeof(logical) - ESP32_MQUICKJS_RPC_SEGMENT_BYTES,
              last, sizeof(last), &last_len) == ESP32_MQUICKJS_RPC_WIRE_OK);

    esp32_mquickjs_rpc_wire_decoder_init(&decoder);
    esp32_mquickjs_rpc_wire_decoder_feed(
        &decoder, first, first_len, 10, capture_message, capture_stream,
        capture_error, &capture);
    CHECK(capture.messages == 0);
    CHECK(capture.stream_len == ESP32_MQUICKJS_RPC_SEGMENT_BYTES - prefix_len);
    esp32_mquickjs_rpc_wire_decoder_feed(
        &decoder, last, last_len, 11, capture_message, capture_stream,
        capture_error, &capture);
    CHECK(capture.messages == 1);
    CHECK(capture.logical_len == sizeof(logical));
    CHECK(capture.payload_len == prefix_len);
    CHECK(memcmp(capture.payload, logical, prefix_len) == 0);
    CHECK(capture.stream_len == sizeof(logical) - prefix_len);
    CHECK(memcmp(capture.stream, logical + prefix_len, capture.stream_len) == 0);
    CHECK(esp32_mquickjs_rpc_crc32(capture.stream, capture.stream_len) ==
          esp32_mquickjs_rpc_crc32(logical + prefix_len,
                                   sizeof(logical) - prefix_len));
    esp32_mquickjs_rpc_wire_decoder_reset(&decoder);
    return 0;
}

int main(void)
{
    if (test_crc_and_cobs_all_bytes() != 0 ||
        test_shared_golden_vector() != 0 ||
        test_arbitrary_slices_and_noise_resync() != 0 ||
        test_segmentation_crc_interleave_and_timeout() != 0 ||
        test_transparent_stream() != 0) {
        return 1;
    }
    puts("rpc wire tests passed");
    return 0;
}
