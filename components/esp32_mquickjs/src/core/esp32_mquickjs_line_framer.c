#include "utils/esp32_mquickjs_line_framer.h"

#include <string.h>

void esp32_mquickjs_line_framer_init(esp32_mquickjs_line_framer_t *framer,
                                     char *buffer,
                                     size_t capacity)
{
    if (framer == NULL) {
        return;
    }
    memset(framer, 0, sizeof(*framer));
    framer->buffer = buffer;
    framer->capacity = capacity;
}

size_t esp32_mquickjs_line_framer_feed(esp32_mquickjs_line_framer_t *framer,
                                       const uint8_t *data,
                                       size_t data_len,
                                       esp32_mquickjs_line_framer_emit_fn emit,
                                       void *opaque)
{
    size_t emitted = 0;
    size_t i;

    if (framer == NULL || framer->buffer == NULL || framer->capacity == 0 ||
        data == NULL || emit == NULL) {
        return 0;
    }

    for (i = 0; i < data_len; ++i) {
        uint8_t ch = data[i];

        if (framer->swallow_lf) {
            framer->swallow_lf = false;
            if (ch == '\n') {
                continue;
            }
        }

        if (ch == '\r' || ch == '\n') {
            if (framer->dropping) {
                emit(opaque, NULL, 0, true);
            } else {
                emit(opaque, framer->buffer, framer->length, false);
            }
            emitted++;
            framer->length = 0;
            framer->dropping = false;
            framer->swallow_lf = ch == '\r';
            continue;
        }

        if (framer->dropping) {
            continue;
        }
        if (framer->length >= framer->capacity) {
            framer->dropping = true;
            continue;
        }
        framer->buffer[framer->length++] = (char)ch;
    }

    return emitted;
}
