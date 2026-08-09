#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "utils/esp32_mquickjs_line_framer.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct {
    char frames[8][32];
    size_t lengths[8];
    bool overflow[8];
    size_t count;
} capture_t;

static void capture_frame(void *opaque,
                          const char *frame,
                          size_t frame_len,
                          bool overflow)
{
    capture_t *capture = opaque;
    size_t copy_len = frame_len;

    if (capture == NULL || capture->count >= 8) {
        return;
    }
    if (copy_len >= sizeof(capture->frames[0])) {
        copy_len = sizeof(capture->frames[0]) - 1;
    }
    if (frame != NULL && copy_len > 0) {
        memcpy(capture->frames[capture->count], frame, copy_len);
    }
    capture->frames[capture->count][copy_len] = '\0';
    capture->lengths[capture->count] = frame_len;
    capture->overflow[capture->count] = overflow;
    capture->count++;
}

static int test_split_and_line_endings(void)
{
    esp32_mquickjs_line_framer_t framer;
    capture_t capture = {0};
    char buffer[16];
    const uint8_t first[] = "one\r";
    const uint8_t second[] = "\ntwo\nthree\rfour";
    const uint8_t final[] = "\n";

    esp32_mquickjs_line_framer_init(&framer, buffer, sizeof(buffer));
    CHECK(esp32_mquickjs_line_framer_feed(
              &framer, first, sizeof(first) - 1, capture_frame, &capture) == 1);
    CHECK(esp32_mquickjs_line_framer_feed(
              &framer, second, sizeof(second) - 1, capture_frame, &capture) == 2);
    CHECK(esp32_mquickjs_line_framer_feed(
              &framer, final, sizeof(final) - 1, capture_frame, &capture) == 1);
    CHECK(capture.count == 4);
    CHECK(strcmp(capture.frames[0], "one") == 0);
    CHECK(strcmp(capture.frames[1], "two") == 0);
    CHECK(strcmp(capture.frames[2], "three") == 0);
    CHECK(strcmp(capture.frames[3], "four") == 0);
    return 0;
}

static int test_overflow_recovers_at_next_frame(void)
{
    esp32_mquickjs_line_framer_t framer;
    capture_t capture = {0};
    char buffer[4];
    const uint8_t input[] = "12345\nok\n";

    esp32_mquickjs_line_framer_init(&framer, buffer, sizeof(buffer));
    CHECK(esp32_mquickjs_line_framer_feed(
              &framer, input, sizeof(input) - 1, capture_frame, &capture) == 2);
    CHECK(capture.count == 2);
    CHECK(capture.overflow[0]);
    CHECK(capture.lengths[0] == 0);
    CHECK(!capture.overflow[1]);
    CHECK(strcmp(capture.frames[1], "ok") == 0);
    return 0;
}

static int test_exact_capacity_is_valid(void)
{
    esp32_mquickjs_line_framer_t framer;
    capture_t capture = {0};
    char buffer[4];
    const uint8_t input[] = "1234\n";

    esp32_mquickjs_line_framer_init(&framer, buffer, sizeof(buffer));
    esp32_mquickjs_line_framer_feed(
        &framer, input, sizeof(input) - 1, capture_frame, &capture);
    CHECK(capture.count == 1);
    CHECK(!capture.overflow[0]);
    CHECK(capture.lengths[0] == 4);
    CHECK(memcmp(capture.frames[0], "1234", 4) == 0);
    return 0;
}

int main(void)
{
    if (test_split_and_line_endings() != 0 ||
        test_overflow_recovers_at_next_frame() != 0 ||
        test_exact_capacity_is_valid() != 0) {
        return 1;
    }
    puts("line framer tests passed");
    return 0;
}
