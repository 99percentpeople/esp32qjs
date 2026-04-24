#pragma once

#include <stdint.h>

typedef struct {
    const char *name;
    const uint8_t *glyphs;
    uint8_t first;
    uint8_t last;
    uint8_t width;
    uint8_t height;
    uint8_t bytes_per_column;
    uint8_t advance;
    uint8_t line_height;
} esp32_mquickjs_bitmap_font_t;

const uint8_t *esp32_mquickjs_bitmap_font_glyph(const esp32_mquickjs_bitmap_font_t *font, char ch);

void esp32_mquickjs_bitmap_font_measure(const esp32_mquickjs_bitmap_font_t *font,
                                        const char *text,
                                        int spacing,
                                        int *out_width,
                                        int *out_height,
                                        int *out_lines);
