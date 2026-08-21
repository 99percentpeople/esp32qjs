#include "utils/esp32_mquickjs_font.h"

#include <stddef.h>

const uint8_t *esp32_mquickjs_bitmap_font_glyph(const esp32_mquickjs_bitmap_font_t *font, char ch)
{
    unsigned char c = (unsigned char)ch;

    if (font == NULL || font->glyphs == NULL || font->width == 0 || font->bytes_per_column == 0) {
        return NULL;
    }
    if (c < font->first || c > font->last) {
        c = '?';
    }
    if (c < font->first || c > font->last) {
        c = font->first;
    }
    return font->glyphs + ((size_t)(c - font->first) * font->width * font->bytes_per_column);
}

void esp32_mquickjs_bitmap_font_measure(const esp32_mquickjs_bitmap_font_t *font,
                                        const char *text,
                                        int spacing,
                                        int *out_width,
                                        int *out_height,
                                        int *out_lines)
{
    int line_width = 0;
    int max_width = 0;
    int lines = 1;
    int advance = font != NULL ? font->advance : 0;
    int line_height = font != NULL ? font->line_height : 0;
    const char *cursor;

    if (text == NULL) {
        text = "";
    }
    for (cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor == '\n') {
            if (line_width > max_width) {
                max_width = line_width;
            }
            line_width = 0;
            lines++;
            continue;
        }
        line_width += advance + spacing;
    }
    if (line_width > max_width) {
        max_width = line_width;
    }
    if (max_width > 0) {
        max_width -= spacing;
    }
    *out_width = max_width;
    *out_height = lines * line_height;
    *out_lines = lines;
}
