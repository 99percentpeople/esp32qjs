/* Call the production parser with guarded readable spans, not a test parser. */
#define _GNU_SOURCE
#include "esp32_mquickjs_wifi_rx.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void put_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = value; bytes[1] = value >> 8;
}

static void packet(uint8_t bytes[64], uint16_t fc)
{
    memset(bytes, 0xee, 64);
    put_u16(bytes, fc); put_u16(bytes + 2, 0x4523);
    memset(bytes + 4, 1, 6); memset(bytes + 10, 2, 6);
    memset(bytes + 16, 3, 6); memset(bytes + 24, 4, 6);
    put_u16(bytes + 22, 0xabcd);
}

static void address(const esp32_mquickjs_wifi_rx_header_t *header,
                    esp32_mquickjs_wifi_rx_address_t role, unsigned marker)
{
    assert(header->address_mask & (1U << role));
    for (unsigned i = 0; i < 6; ++i) assert(header->addresses[role][i] == marker);
}

static void test_address_roles(void)
{
    uint8_t bytes[64]; esp32_mquickjs_wifi_rx_header_t h;
    /* Golden DS mapping: DA/SA/BSSID address indices for the four DS states. */
    static const unsigned expected[4][3] = {{1,2,3},{3,2,1},{1,3,2},{3,4,0}};
    for (unsigned ds = 0; ds < 4; ++ds) {
        packet(bytes, 0x0008 | ds << 8);
        assert(esp32_mquickjs_wifi_rx_parse_header(bytes, 64, &h) == ESP32_MQUICKJS_WIFI_RX_PARSED);
        address(&h, ESP32_MQUICKJS_WIFI_RX_RECEIVER, 1);
        address(&h, ESP32_MQUICKJS_WIFI_RX_TRANSMITTER, 2);
        address(&h, ESP32_MQUICKJS_WIFI_RX_DESTINATION, expected[ds][0]);
        address(&h, ESP32_MQUICKJS_WIFI_RX_SOURCE, expected[ds][1]);
        if (ds == 3) assert(!(h.address_mask & (1U << ESP32_MQUICKJS_WIFI_RX_BSSID)));
        else address(&h, ESP32_MQUICKJS_WIFI_RX_BSSID, expected[ds][2]);
        assert(h.sequence_valid && h.sequence_control == 0xabcd && !h.qos_valid && !h.ht_valid);
    }
    packet(bytes, 0x0080);
    assert(esp32_mquickjs_wifi_rx_parse_header(bytes, 24, &h) == ESP32_MQUICKJS_WIFI_RX_PARSED);
    assert(h.address_mask == 31);address(&h, ESP32_MQUICKJS_WIFI_RX_BSSID, 3);
    for (unsigned subtype = 12; subtype <= 13; ++subtype) {
        packet(bytes, subtype << 4 | 4);
        assert(esp32_mquickjs_wifi_rx_parse_header(bytes, 10, &h) == ESP32_MQUICKJS_WIFI_RX_PARSED);
        assert(h.address_mask == ((1U << ESP32_MQUICKJS_WIFI_RX_RECEIVER) | (1U << ESP32_MQUICKJS_WIFI_RX_DESTINATION)));
        assert(!h.sequence_valid && !h.ht_valid);
    }
    packet(bytes, 0x00a4);
    assert(esp32_mquickjs_wifi_rx_parse_header(bytes, 16, &h) == ESP32_MQUICKJS_WIFI_RX_PARSED);
    address(&h, ESP32_MQUICKJS_WIFI_RX_BSSID, 1);address(&h, ESP32_MQUICKJS_WIFI_RX_SOURCE, 2);
    packet(bytes, 0x00e4);
    assert(esp32_mquickjs_wifi_rx_parse_header(bytes, 16, &h) == ESP32_MQUICKJS_WIFI_RX_PARSED);
    address(&h, ESP32_MQUICKJS_WIFI_RX_BSSID, 2);
    packet(bytes, 0x0074);
    assert(esp32_mquickjs_wifi_rx_parse_header(bytes, 16, &h) == ESP32_MQUICKJS_WIFI_RX_PARSED);
    assert(!(h.address_mask & (1U << ESP32_MQUICKJS_WIFI_RX_TRANSMITTER)) && h.ht_valid);
}

static void test_variable_headers(void)
{
    static const struct { uint16_t fc; uint8_t length; } cases[] = {
        {0x0080,24},{0x8080,28},{0x0008,24},{0x8008,24},{0x0308,30},
        {0x0088,26},{0x8388,36},{0x8308,30},{0x00d4,10},{0x00c4,10},
        {0x00b4,16},{0x0074,16},{0x0084,16},{0x0094,16},{0x00a4,16},
        {0x00e4,16},{0x0054,16},
    };
    uint8_t bytes[64]; esp32_mquickjs_wifi_rx_header_t h;
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        packet(bytes, cases[i].fc);
        assert(esp32_mquickjs_wifi_rx_parse_header(bytes, cases[i].length - 1, &h) == ESP32_MQUICKJS_WIFI_RX_SHORT_HEADER);
        assert(h.header_length == cases[i].length && !h.address_mask && !h.sequence_valid && !h.qos_valid && !h.ht_valid);
        assert(esp32_mquickjs_wifi_rx_parse_header(bytes, cases[i].length, &h) == ESP32_MQUICKJS_WIFI_RX_PARSED);
        assert(h.header_length == cases[i].length && h.duration_valid && h.duration_id == 0x4523);
    }
    packet(bytes, 0x8388);put_u16(bytes + 30, 0x4321);
    bytes[32]=0x78;bytes[33]=0x56;bytes[34]=0x34;bytes[35]=0x12;
    assert(esp32_mquickjs_wifi_rx_parse_header(bytes, 36, &h) == ESP32_MQUICKJS_WIFI_RX_PARSED);
    assert(h.qos_valid && h.qos_control == 0x4321 && h.ht_valid && h.ht_control == 0x12345678);
    address(&h, ESP32_MQUICKJS_WIFI_RX_SOURCE, 4);
    packet(bytes, 0x4088); /* Protected QoS body stays opaque; MAC header remains readable. */
    assert(esp32_mquickjs_wifi_rx_parse_header(bytes, 26, &h) == ESP32_MQUICKJS_WIFI_RX_PARSED);
    assert(h.frame_control & 0x4000);
}

static void test_unavailable_layouts(void)
{
    uint8_t bytes[64];esp32_mquickjs_wifi_rx_header_t h;
    assert(esp32_mquickjs_wifi_rx_parse_header(NULL, 0, &h) == ESP32_MQUICKJS_WIFI_RX_SHORT_HEADER);
    assert(!h.frame_control_valid && !h.address_mask);
    assert(esp32_mquickjs_wifi_rx_parse_header(NULL, 10, &h) == ESP32_MQUICKJS_WIFI_RX_INVALID_ARGUMENT);
    assert(esp32_mquickjs_wifi_rx_parse_header(bytes, 0, NULL) == ESP32_MQUICKJS_WIFI_RX_INVALID_ARGUMENT);
    static const uint16_t unknown[] = {0x000c,0x0070,0x00f0,0x0004,0x0064,0x00d8};
    for (size_t i = 0; i < sizeof(unknown)/sizeof(unknown[0]); ++i) {
        packet(bytes, unknown[i]);
        assert(esp32_mquickjs_wifi_rx_parse_header(bytes, 64, &h) == ESP32_MQUICKJS_WIFI_RX_UNSUPPORTED_LAYOUT);
        assert(h.frame_control_valid && !h.header_length && !h.address_mask);
    }
    for (unsigned version = 1; version <= 3; ++version) {
        packet(bytes, 0x0080 | version);
        assert(esp32_mquickjs_wifi_rx_parse_header(bytes, 64, &h) == ESP32_MQUICKJS_WIFI_RX_UNSUPPORTED_VERSION);
        assert(h.version == version && !h.address_mask && !h.duration_valid && h.type == ESP32_MQUICKJS_WIFI_PACKET_UNKNOWN);
    }
    packet(bytes, 0x0180);
    assert(esp32_mquickjs_wifi_rx_parse_header(bytes, 64, &h) == ESP32_MQUICKJS_WIFI_RX_INVALID_FLAGS);
    assert(!h.address_mask);
    assert(!esp32_mquickjs_wifi_rx_subtype_name(ESP32_MQUICKJS_WIFI_PACKET_MISC, 0));
    assert(!esp32_mquickjs_wifi_rx_subtype_name(ESP32_MQUICKJS_WIFI_PACKET_DATA, 255));
    assert(!strcmp(esp32_mquickjs_wifi_rx_subtype_name(ESP32_MQUICKJS_WIFI_PACKET_MANAGEMENT, 8), "beacon"));
}

static void test_guarded_short_spans(void)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t *mapping = mmap(NULL, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(mapping != MAP_FAILED && page >= 64);
    assert(mprotect(mapping + page, page, PROT_READ | PROT_WRITE) == 0);
    /* Exercise every frame-control bit pattern at every length from 0 to 64.
     * For each parsed result, available fields must belong to the declared span.
     * The trailing guard page catches overreads; the snapshot detects writes. */
    for (size_t length = 0; length <= 64; ++length) {
        for (uint32_t fc = 0; fc <= UINT16_MAX; ++fc) {
            uint8_t before[64],*bytes = mapping + page * 2 - length;
            memset(bytes, 0xa5, length);
            if (length >= 1) bytes[0] = fc;
            if (length >= 2) bytes[1] = fc >> 8;
            memcpy(before, bytes, length);
            esp32_mquickjs_wifi_rx_header_t h;memset(&h, 0x55, sizeof(h));
            esp32_mquickjs_wifi_rx_parse_status_t status = esp32_mquickjs_wifi_rx_parse_header(bytes, length, &h);
            assert(status == h.status && !memcmp(before, bytes, length));
            if (status == ESP32_MQUICKJS_WIFI_RX_PARSED) assert(h.header_length <= length && h.header_length >= 10);
            else assert(!h.address_mask && !h.sequence_valid && !h.qos_valid && !h.ht_valid);
        }
    }
    assert(munmap(mapping, page * 3) == 0);
}

int main(void)
{
    test_address_roles();test_variable_headers();test_unavailable_layouts();test_guarded_short_spans();
    puts("production Wi-Fi MAC header parser cases passed");
    return 0;
}
