#include "esp32_mquickjs_display_buffer_internal.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_DISPLAY_BUFFER

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_heap_caps.h"

#define DISPLAY_BUFFER_STACK_POINTS 32U

typedef struct {
    int32_t x;
    int32_t y;
} display_buffer_point_t;

static uint32_t abs_i32_to_u32(int32_t value)
{
    return value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}

static uint32_t clamp_radius_u32(uint32_t value)
{
    return value > DISPLAY_BUFFER_MAX_DIMENSION ? DISPLAY_BUFFER_MAX_DIMENSION : value;
}

static uint32_t isqrt_u64(uint64_t value)
{
    uint64_t result = 0;
    uint64_t bit = 1ULL << 62;

    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

static void fill_span_raw(esp32_mquickjs_display_buffer_t *buffer,
                          int64_t x0,
                          int y,
                          int64_t x1,
                          uint16_t color,
                          int *dirty_x0,
                          int *dirty_y0,
                          int *dirty_x1,
                          int *dirty_y1)
{
    int clipped_x0;
    int clipped_x1;

    if (y < 0 || y >= buffer->height || x1 < x0 || x1 < 0 || x0 >= buffer->width) {
        return;
    }
    clipped_x0 = x0 < 0 ? 0 : (int)x0;
    clipped_x1 = x1 >= buffer->width ? buffer->width - 1 : (int)x1;
    fill_rect_raw(buffer, clipped_x0, y, clipped_x1 - clipped_x0 + 1, 1, color, false);
    if (clipped_x0 < *dirty_x0) {
        *dirty_x0 = clipped_x0;
    }
    if (y < *dirty_y0) {
        *dirty_y0 = y;
    }
    if (clipped_x1 > *dirty_x1) {
        *dirty_x1 = clipped_x1;
    }
    if (y > *dirty_y1) {
        *dirty_y1 = y;
    }
}

static void set_pixel_dirty_raw(esp32_mquickjs_display_buffer_t *buffer,
                                int x,
                                int y,
                                uint16_t color,
                                int *dirty_x0,
                                int *dirty_y0,
                                int *dirty_x1,
                                int *dirty_y1)
{
    if (x < 0 || y < 0 || x >= buffer->width || y >= buffer->height) {
        return;
    }
    set_pixel_raw(buffer, x, y, color);
    if (x < *dirty_x0) {
        *dirty_x0 = x;
    }
    if (y < *dirty_y0) {
        *dirty_y0 = y;
    }
    if (x > *dirty_x1) {
        *dirty_x1 = x;
    }
    if (y > *dirty_y1) {
        *dirty_y1 = y;
    }
}

static bool vertical_radius_range(int32_t center, uint32_t radius, int limit, int *out_start, int *out_end)
{
    int64_t start = (int64_t)center - radius;
    int64_t end = (int64_t)center + radius;

    if (end < 0 || start >= limit) {
        return false;
    }
    *out_start = start < 0 ? 0 : (int)start;
    *out_end = end >= limit ? limit - 1 : (int)end;
    return *out_start <= *out_end;
}

static bool horizontal_radius_range(int32_t center, uint32_t radius, int limit, int *out_start, int *out_end)
{
    int64_t start = (int64_t)center - radius;
    int64_t end = (int64_t)center + radius;

    if (end < 0 || start >= limit) {
        return false;
    }
    *out_start = start < 0 ? 0 : (int)start;
    *out_end = end >= limit ? limit - 1 : (int)end;
    return *out_start <= *out_end;
}

static void mark_dirty_if_touched(esp32_mquickjs_display_buffer_t *buffer,
                                  int dirty_x0,
                                  int dirty_y0,
                                  int dirty_x1,
                                  int dirty_y1)
{
    if (dirty_x1 >= dirty_x0 && dirty_y1 >= dirty_y0) {
        mark_dirty(buffer, dirty_x0, dirty_y0, dirty_x1 - dirty_x0 + 1, dirty_y1 - dirty_y0 + 1);
    }
}

static void draw_circle_raw(esp32_mquickjs_display_buffer_t *buffer,
                            int32_t cx,
                            int32_t cy,
                            uint32_t radius,
                            uint16_t color)
{
    int dirty_x0 = buffer->width;
    int dirty_y0 = buffer->height;
    int dirty_x1 = -1;
    int dirty_y1 = -1;
    int x;
    int y = 0;
    int err;

    radius = clamp_radius_u32(radius);
    x = (int)radius;
    err = 1 - x;
    if (radius == 0) {
        set_pixel_dirty_raw(buffer, cx, cy, color, &dirty_x0, &dirty_y0, &dirty_x1, &dirty_y1);
        mark_dirty_if_touched(buffer, dirty_x0, dirty_y0, dirty_x1, dirty_y1);
        return;
    }

    while (x >= y) {
        set_pixel_dirty_raw(buffer, cx + x, cy + y, color, &dirty_x0, &dirty_y0, &dirty_x1, &dirty_y1);
        set_pixel_dirty_raw(buffer, cx + y, cy + x, color, &dirty_x0, &dirty_y0, &dirty_x1, &dirty_y1);
        set_pixel_dirty_raw(buffer, cx - y, cy + x, color, &dirty_x0, &dirty_y0, &dirty_x1, &dirty_y1);
        set_pixel_dirty_raw(buffer, cx - x, cy + y, color, &dirty_x0, &dirty_y0, &dirty_x1, &dirty_y1);
        set_pixel_dirty_raw(buffer, cx - x, cy - y, color, &dirty_x0, &dirty_y0, &dirty_x1, &dirty_y1);
        set_pixel_dirty_raw(buffer, cx - y, cy - x, color, &dirty_x0, &dirty_y0, &dirty_x1, &dirty_y1);
        set_pixel_dirty_raw(buffer, cx + y, cy - x, color, &dirty_x0, &dirty_y0, &dirty_x1, &dirty_y1);
        set_pixel_dirty_raw(buffer, cx + x, cy - y, color, &dirty_x0, &dirty_y0, &dirty_x1, &dirty_y1);
        y++;
        if (err < 0) {
            err += (y << 1) + 1;
        } else {
            x--;
            err += ((y - x) << 1) + 1;
        }
    }
    mark_dirty_if_touched(buffer, dirty_x0, dirty_y0, dirty_x1, dirty_y1);
}

static void fill_circle_raw(esp32_mquickjs_display_buffer_t *buffer,
                            int32_t cx,
                            int32_t cy,
                            uint32_t radius,
                            uint16_t color)
{
    int dirty_x0 = buffer->width;
    int dirty_y0 = buffer->height;
    int dirty_x1 = -1;
    int dirty_y1 = -1;
    int start_y;
    int end_y;
    uint64_t rr;
    int y;

    radius = clamp_radius_u32(radius);
    rr = (uint64_t)radius * radius;
    if (!vertical_radius_range(cy, radius, buffer->height, &start_y, &end_y)) {
        return;
    }
    for (y = start_y; y <= end_y; ++y) {
        int64_t dy = (int64_t)y - cy;
        uint64_t dy2 = (uint64_t)(dy < 0 ? -dy : dy);
        uint32_t span;

        if (dy2 > radius) {
            continue;
        }
        span = isqrt_u64(rr - dy2 * dy2);
        fill_span_raw(buffer,
                      (int64_t)cx - span,
                      y,
                      (int64_t)cx + span,
                      color,
                      &dirty_x0,
                      &dirty_y0,
                      &dirty_x1,
                      &dirty_y1);
    }
    mark_dirty_if_touched(buffer, dirty_x0, dirty_y0, dirty_x1, dirty_y1);
}

static void draw_line_raw(esp32_mquickjs_display_buffer_t *buffer,
                          int x0,
                          int y0,
                          int x1,
                          int y1,
                          uint16_t color);

static void fill_ellipse_raw(esp32_mquickjs_display_buffer_t *buffer,
                             int32_t cx,
                             int32_t cy,
                             uint32_t rx,
                             uint32_t ry,
                             uint16_t color)
{
    int dirty_x0 = buffer->width;
    int dirty_y0 = buffer->height;
    int dirty_x1 = -1;
    int dirty_y1 = -1;
    int start_y;
    int end_y;
    uint64_t rx2;
    uint64_t ry2;
    int y;

    rx = clamp_radius_u32(rx);
    ry = clamp_radius_u32(ry);
    if (rx == 0 && ry == 0) {
        set_pixel_raw(buffer, cx, cy, color);
        mark_dirty(buffer, cx, cy, 1, 1);
        return;
    }
    if (ry == 0) {
        fill_span_raw(buffer,
                      (int64_t)cx - rx,
                      cy,
                      (int64_t)cx + rx,
                      color,
                      &dirty_x0,
                      &dirty_y0,
                      &dirty_x1,
                      &dirty_y1);
        mark_dirty_if_touched(buffer, dirty_x0, dirty_y0, dirty_x1, dirty_y1);
        return;
    }
    if (rx == 0) {
        int y0;
        int y1;

        if (!vertical_radius_range(cy, ry, buffer->height, &y0, &y1)) {
            return;
        }
        fill_rect_raw(buffer, cx, y0, 1, y1 - y0 + 1, color, true);
        return;
    }

    rx2 = (uint64_t)rx * rx;
    ry2 = (uint64_t)ry * ry;
    if (!vertical_radius_range(cy, ry, buffer->height, &start_y, &end_y)) {
        return;
    }
    for (y = start_y; y <= end_y; ++y) {
        int64_t dy = (int64_t)y - cy;
        uint64_t dy_abs = (uint64_t)(dy < 0 ? -dy : dy);
        uint64_t remaining;
        uint32_t span;

        if (dy_abs > ry) {
            continue;
        }
        remaining = ry2 - dy_abs * dy_abs;
        span = isqrt_u64((rx2 * remaining) / ry2);
        fill_span_raw(buffer,
                      (int64_t)cx - span,
                      y,
                      (int64_t)cx + span,
                      color,
                      &dirty_x0,
                      &dirty_y0,
                      &dirty_x1,
                      &dirty_y1);
    }
    mark_dirty_if_touched(buffer, dirty_x0, dirty_y0, dirty_x1, dirty_y1);
}

static void draw_ellipse_raw(esp32_mquickjs_display_buffer_t *buffer,
                             int32_t cx,
                             int32_t cy,
                             uint32_t rx,
                             uint32_t ry,
                             uint16_t color)
{
    int dirty_x0 = buffer->width;
    int dirty_y0 = buffer->height;
    int dirty_x1 = -1;
    int dirty_y1 = -1;
    int start_y;
    int end_y;
    int start_x;
    int end_x;
    uint64_t rx2;
    uint64_t ry2;
    int x;
    int y;

    rx = clamp_radius_u32(rx);
    ry = clamp_radius_u32(ry);
    if (rx == 0 && ry == 0) {
        set_pixel_dirty_raw(buffer, cx, cy, color, &dirty_x0, &dirty_y0, &dirty_x1, &dirty_y1);
        mark_dirty_if_touched(buffer, dirty_x0, dirty_y0, dirty_x1, dirty_y1);
        return;
    }
    if (ry == 0) {
        draw_line_raw(buffer, cx - (int32_t)rx, cy, cx + (int32_t)rx, cy, color);
        mark_dirty(buffer, cx - (int32_t)rx, cy, (int)rx * 2 + 1, 1);
        return;
    }
    if (rx == 0) {
        draw_line_raw(buffer, cx, cy - (int32_t)ry, cx, cy + (int32_t)ry, color);
        mark_dirty(buffer, cx, cy - (int32_t)ry, 1, (int)ry * 2 + 1);
        return;
    }

    rx2 = (uint64_t)rx * rx;
    ry2 = (uint64_t)ry * ry;
    if (!vertical_radius_range(cy, ry, buffer->height, &start_y, &end_y)) {
        return;
    }
    for (y = start_y; y <= end_y; ++y) {
        int64_t dy = (int64_t)y - cy;
        uint64_t dy_abs = (uint64_t)(dy < 0 ? -dy : dy);
        uint64_t remaining;
        uint32_t span;

        if (dy_abs > ry) {
            continue;
        }
        remaining = ry2 - dy_abs * dy_abs;
        span = isqrt_u64((rx2 * remaining) / ry2);
        set_pixel_dirty_raw(buffer, cx - (int32_t)span, y, color, &dirty_x0, &dirty_y0, &dirty_x1, &dirty_y1);
        set_pixel_dirty_raw(buffer, cx + (int32_t)span, y, color, &dirty_x0, &dirty_y0, &dirty_x1, &dirty_y1);
    }
    if (horizontal_radius_range(cx, rx, buffer->width, &start_x, &end_x)) {
        for (x = start_x; x <= end_x; ++x) {
            int64_t dx = (int64_t)x - cx;
            uint64_t dx_abs = (uint64_t)(dx < 0 ? -dx : dx);
            uint64_t remaining;
            uint32_t span;

            if (dx_abs > rx) {
                continue;
            }
            remaining = rx2 - dx_abs * dx_abs;
            span = isqrt_u64((ry2 * remaining) / rx2);
            set_pixel_dirty_raw(buffer, x, cy - (int32_t)span, color, &dirty_x0, &dirty_y0, &dirty_x1, &dirty_y1);
            set_pixel_dirty_raw(buffer, x, cy + (int32_t)span, color, &dirty_x0, &dirty_y0, &dirty_x1, &dirty_y1);
        }
    }
    mark_dirty_if_touched(buffer, dirty_x0, dirty_y0, dirty_x1, dirty_y1);
}

static int32_t round_double_to_i32(double value)
{
    return value >= 0 ? (int32_t)(value + 0.5) : (int32_t)(value - 0.5);
}

static void expand_line_bounds(int x0,
                               int y0,
                               int x1,
                               int y1,
                               int *min_x,
                               int *min_y,
                               int *max_x,
                               int *max_y)
{
    if (x0 < *min_x) {
        *min_x = x0;
    }
    if (x1 < *min_x) {
        *min_x = x1;
    }
    if (y0 < *min_y) {
        *min_y = y0;
    }
    if (y1 < *min_y) {
        *min_y = y1;
    }
    if (x0 > *max_x) {
        *max_x = x0;
    }
    if (x1 > *max_x) {
        *max_x = x1;
    }
    if (y0 > *max_y) {
        *max_y = y0;
    }
    if (y1 > *max_y) {
        *max_y = y1;
    }
}

static void draw_line_raw(esp32_mquickjs_display_buffer_t *buffer,
                          int x0,
                          int y0,
                          int x1,
                          int y1,
                          uint16_t color)
{
    int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy_abs = y1 >= y0 ? y1 - y0 : y0 - y1;
    int dy = -dy_abs;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        int e2;

        set_pixel_raw(buffer, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        e2 = err << 1;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void draw_round_rect_raw(esp32_mquickjs_display_buffer_t *buffer,
                                int32_t x,
                                int32_t y,
                                int32_t width,
                                int32_t height,
                                uint32_t radius,
                                uint16_t color)
{
    int32_t r;
    int32_t cx0;
    int32_t cx1;
    int32_t cy0;
    int32_t cy1;
    int px;
    int py = 0;
    int err;

    if (width <= 0 || height <= 0) {
        return;
    }
    r = (int32_t)clamp_radius_u32(radius);
    if (r > width / 2) {
        r = width / 2;
    }
    if (r > height / 2) {
        r = height / 2;
    }
    if (r <= 0) {
        draw_line_raw(buffer, x, y, x + width - 1, y, color);
        draw_line_raw(buffer, x, y + height - 1, x + width - 1, y + height - 1, color);
        draw_line_raw(buffer, x, y, x, y + height - 1, color);
        draw_line_raw(buffer, x + width - 1, y, x + width - 1, y + height - 1, color);
        mark_dirty(buffer, x, y, width, height);
        return;
    }

    cx0 = x + r;
    cx1 = x + width - r - 1;
    cy0 = y + r;
    cy1 = y + height - r - 1;
    draw_line_raw(buffer, cx0, y, cx1, y, color);
    draw_line_raw(buffer, cx0, y + height - 1, cx1, y + height - 1, color);
    draw_line_raw(buffer, x, cy0, x, cy1, color);
    draw_line_raw(buffer, x + width - 1, cy0, x + width - 1, cy1, color);

    px = r;
    err = 1 - px;
    while (px >= py) {
        set_pixel_raw(buffer, cx0 - px, cy0 - py, color);
        set_pixel_raw(buffer, cx0 - py, cy0 - px, color);
        set_pixel_raw(buffer, cx1 + px, cy0 - py, color);
        set_pixel_raw(buffer, cx1 + py, cy0 - px, color);
        set_pixel_raw(buffer, cx0 - px, cy1 + py, color);
        set_pixel_raw(buffer, cx0 - py, cy1 + px, color);
        set_pixel_raw(buffer, cx1 + px, cy1 + py, color);
        set_pixel_raw(buffer, cx1 + py, cy1 + px, color);
        py++;
        if (err < 0) {
            err += (py << 1) + 1;
        } else {
            px--;
            err += ((py - px) << 1) + 1;
        }
    }
    mark_dirty(buffer, x, y, width, height);
}

static void fill_round_rect_raw(esp32_mquickjs_display_buffer_t *buffer,
                                int32_t x,
                                int32_t y,
                                int32_t width,
                                int32_t height,
                                uint32_t radius,
                                uint16_t color)
{
    int32_t r;
    uint64_t rr;
    int dy;

    if (width <= 0 || height <= 0) {
        return;
    }
    r = (int32_t)clamp_radius_u32(radius);
    if (r > width / 2) {
        r = width / 2;
    }
    if (r > height / 2) {
        r = height / 2;
    }
    if (r <= 0) {
        fill_rect_raw(buffer, x, y, width, height, color, true);
        return;
    }

    fill_rect_raw(buffer, x, y + r, width, height - r * 2, color, false);
    rr = (uint64_t)r * r;
    for (dy = 0; dy < r; ++dy) {
        int32_t edge_y = r - dy;
        int32_t span = (int32_t)isqrt_u64(rr - (uint64_t)edge_y * edge_y);
        int32_t row_x = x + r - span;
        int32_t row_width = width - r * 2 + span * 2;

        if (row_width > 0) {
            fill_rect_raw(buffer, row_x, y + dy, row_width, 1, color, false);
            fill_rect_raw(buffer, row_x, y + height - 1 - dy, row_width, 1, color, false);
        }
    }
    mark_dirty(buffer, x, y, width, height);
}

static void draw_quadratic_bezier_raw(esp32_mquickjs_display_buffer_t *buffer,
                                      int32_t x0,
                                      int32_t y0,
                                      int32_t cx,
                                      int32_t cy,
                                      int32_t x1,
                                      int32_t y1,
                                      uint16_t color,
                                      uint32_t segments)
{
    int prev_x = x0;
    int prev_y = y0;
    int min_x = x0;
    int min_y = y0;
    int max_x = x0;
    int max_y = y0;
    uint32_t i;

    for (i = 1; i <= segments; ++i) {
        double t = (double)i / (double)segments;
        double mt = 1.0 - t;
        int x = round_double_to_i32(mt * mt * x0 + 2.0 * mt * t * cx + t * t * x1);
        int y = round_double_to_i32(mt * mt * y0 + 2.0 * mt * t * cy + t * t * y1);

        draw_line_raw(buffer, prev_x, prev_y, x, y, color);
        expand_line_bounds(prev_x, prev_y, x, y, &min_x, &min_y, &max_x, &max_y);
        prev_x = x;
        prev_y = y;
    }
    mark_dirty(buffer, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

static void draw_cubic_bezier_raw(esp32_mquickjs_display_buffer_t *buffer,
                                  int32_t x0,
                                  int32_t y0,
                                  int32_t c1x,
                                  int32_t c1y,
                                  int32_t c2x,
                                  int32_t c2y,
                                  int32_t x1,
                                  int32_t y1,
                                  uint16_t color,
                                  uint32_t segments)
{
    int prev_x = x0;
    int prev_y = y0;
    int min_x = x0;
    int min_y = y0;
    int max_x = x0;
    int max_y = y0;
    uint32_t i;

    for (i = 1; i <= segments; ++i) {
        double t = (double)i / (double)segments;
        double mt = 1.0 - t;
        double mt2 = mt * mt;
        double t2 = t * t;
        int x = round_double_to_i32(mt2 * mt * x0 + 3.0 * mt2 * t * c1x + 3.0 * mt * t2 * c2x + t2 * t * x1);
        int y = round_double_to_i32(mt2 * mt * y0 + 3.0 * mt2 * t * c1y + 3.0 * mt * t2 * c2y + t2 * t * y1);

        draw_line_raw(buffer, prev_x, prev_y, x, y, color);
        expand_line_bounds(prev_x, prev_y, x, y, &min_x, &min_y, &max_x, &max_y);
        prev_x = x;
        prev_y = y;
    }
    mark_dirty(buffer, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

static int32_t ceil_fixed_16(int64_t value)
{
    if (value >= 0) {
        return (int32_t)((value + 65535) >> 16);
    }
    return (int32_t)(-((-value) >> 16));
}

static int32_t floor_fixed_16(int64_t value)
{
    if (value >= 0) {
        return (int32_t)(value >> 16);
    }
    return (int32_t)(-(((-value) + 65535) >> 16));
}

static void sort_fixed_intersections(int64_t *values, uint32_t count)
{
    uint32_t i;

    for (i = 1; i < count; ++i) {
        int64_t value = values[i];
        uint32_t j = i;

        while (j > 0 && values[j - 1] > value) {
            values[j] = values[j - 1];
            --j;
        }
        values[j] = value;
    }
}

static bool point_list_get_length(JSContext *ctx, JSValue points, const char *api_name, uint32_t *out_length)
{
    JSGCRef length_ref;
    JSValue *length_value;

    if (JS_GetClassID(ctx, points) < 0) {
        JS_ThrowTypeError(ctx, "%s expects a point array", api_name);
        return false;
    }
    length_value = JS_PushGCRef(ctx, &length_ref);
    *length_value = JS_GetPropertyStr(ctx, points, "length");
    if (JS_IsException(*length_value)) {
        JS_PopGCRef(ctx, &length_ref);
        return false;
    }
    if (!JS_IsNumber(ctx, *length_value) || !value_to_u32(ctx, *length_value, out_length)) {
        JS_PopGCRef(ctx, &length_ref);
        JS_ThrowTypeError(ctx, "%s expects a point array", api_name);
        return false;
    }
    JS_PopGCRef(ctx, &length_ref);
    return true;
}

static bool parse_point_list(JSContext *ctx,
                             JSValue points_value,
                             const char *api_name,
                             display_buffer_point_t *stack_points,
                             uint32_t stack_capacity,
                             display_buffer_point_t **out_points,
                             uint32_t *out_count,
                             bool *out_owned)
{
    JSGCRef item_ref;
    JSGCRef property_ref;
    JSValue *item;
    JSValue *property;
    display_buffer_point_t *points = NULL;
    uint32_t length = 0;
    uint32_t count = 0;
    uint32_t i;
    bool flat = false;

    *out_points = NULL;
    *out_count = 0;
    *out_owned = false;
    if (!point_list_get_length(ctx, points_value, api_name, &length)) {
        return false;
    }
    if (length == 0) {
        *out_points = stack_points;
        return true;
    }

    item = JS_PushGCRef(ctx, &item_ref);
    property = JS_PushGCRef(ctx, &property_ref);
    *item = JS_GetPropertyUint32(ctx, points_value, 0);
    if (JS_IsException(*item)) {
        goto fail_exception;
    }
    flat = JS_IsNumber(ctx, *item);
    if (flat) {
        if ((length & 1U) != 0) {
            JS_ThrowTypeError(ctx, "%s expects an even-length flat point array", api_name);
            goto fail;
        }
        count = length >> 1U;
    } else {
        count = length;
    }
    if (count == 0) {
        *out_points = stack_points;
        JS_PopGCRef(ctx, &property_ref);
        JS_PopGCRef(ctx, &item_ref);
        return true;
    }
    if (count <= stack_capacity) {
        points = stack_points;
    } else {
        if (count > SIZE_MAX / sizeof(*points)) {
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }
        points = heap_caps_malloc((size_t)count * sizeof(*points), MALLOC_CAP_8BIT);
        if (points == NULL) {
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }
        *out_owned = true;
    }

    if (flat) {
        for (i = 0; i < count; ++i) {
            *item = JS_GetPropertyUint32(ctx, points_value, i * 2U);
            if (JS_IsException(*item) || !value_to_i32(ctx, *item, &points[i].x)) {
                JS_ThrowTypeError(ctx, "%s expects numeric point coordinates", api_name);
                goto fail;
            }
            *item = JS_GetPropertyUint32(ctx, points_value, i * 2U + 1U);
            if (JS_IsException(*item) || !value_to_i32(ctx, *item, &points[i].y)) {
                JS_ThrowTypeError(ctx, "%s expects numeric point coordinates", api_name);
                goto fail;
            }
        }
    } else {
        for (i = 0; i < count; ++i) {
            *item = JS_GetPropertyUint32(ctx, points_value, i);
            if (JS_IsException(*item)) {
                goto fail_exception;
            }
            if (JS_GetClassID(ctx, *item) < 0) {
                JS_ThrowTypeError(ctx, "%s expects points shaped as [x, y] or { x, y }", api_name);
                goto fail;
            }

            *property = JS_GetPropertyStr(ctx, *item, "length");
            if (JS_IsException(*property)) {
                goto fail_exception;
            }
            if (JS_IsNumber(ctx, *property)) {
                *property = JS_GetPropertyUint32(ctx, *item, 0);
                if (JS_IsException(*property) || !value_to_i32(ctx, *property, &points[i].x)) {
                    JS_ThrowTypeError(ctx, "%s expects numeric point coordinates", api_name);
                    goto fail;
                }
                *property = JS_GetPropertyUint32(ctx, *item, 1);
                if (JS_IsException(*property) || !value_to_i32(ctx, *property, &points[i].y)) {
                    JS_ThrowTypeError(ctx, "%s expects numeric point coordinates", api_name);
                    goto fail;
                }
            } else {
                *property = JS_GetPropertyStr(ctx, *item, "x");
                if (JS_IsException(*property) || !value_to_i32(ctx, *property, &points[i].x)) {
                    JS_ThrowTypeError(ctx, "%s expects numeric point coordinates", api_name);
                    goto fail;
                }
                *property = JS_GetPropertyStr(ctx, *item, "y");
                if (JS_IsException(*property) || !value_to_i32(ctx, *property, &points[i].y)) {
                    JS_ThrowTypeError(ctx, "%s expects numeric point coordinates", api_name);
                    goto fail;
                }
            }
        }
    }

    *out_points = points;
    *out_count = count;
    JS_PopGCRef(ctx, &property_ref);
    JS_PopGCRef(ctx, &item_ref);
    return true;

fail_exception:
    if (!JS_IsException(*item) && !JS_IsException(*property)) {
        JS_ThrowInternalError(ctx, "%s failed while reading points", api_name);
    }
fail:
    if (*out_owned && points != NULL) {
        heap_caps_free(points);
    }
    *out_points = NULL;
    *out_count = 0;
    *out_owned = false;
    JS_PopGCRef(ctx, &property_ref);
    JS_PopGCRef(ctx, &item_ref);
    return false;
}

static bool fill_polygon_raw(esp32_mquickjs_display_buffer_t *buffer,
                             const display_buffer_point_t *points,
                             uint32_t count,
                             uint16_t color,
                             int64_t *stack_intersections,
                             uint32_t stack_capacity)
{
    int64_t *intersections = stack_intersections;
    bool owns_intersections = false;
    int32_t min_y;
    int32_t max_y;
    int start_y;
    int end_y;
    int dirty_x0 = buffer->width;
    int dirty_y0 = buffer->height;
    int dirty_x1 = -1;
    int dirty_y1 = -1;
    uint32_t i;
    int y;

    if (count < 3) {
        return true;
    }
    if (count > stack_capacity) {
        if (count > SIZE_MAX / sizeof(*intersections)) {
            return false;
        }
        intersections = heap_caps_malloc((size_t)count * sizeof(*intersections), MALLOC_CAP_8BIT);
        if (intersections == NULL) {
            return false;
        }
        owns_intersections = true;
    }

    min_y = points[0].y;
    max_y = points[0].y;
    for (i = 1; i < count; ++i) {
        if (points[i].y < min_y) {
            min_y = points[i].y;
        }
        if (points[i].y > max_y) {
            max_y = points[i].y;
        }
    }
    if (max_y < 0 || min_y >= buffer->height) {
        if (owns_intersections) {
            heap_caps_free(intersections);
        }
        return true;
    }
    start_y = min_y < 0 ? 0 : (int)min_y;
    end_y = max_y >= buffer->height ? buffer->height - 1 : (int)max_y;

    for (y = start_y; y <= end_y; ++y) {
        int64_t scan_y2 = ((int64_t)y << 1) + 1;
        uint32_t intersections_count = 0;
        uint32_t edge;

        for (edge = 0; edge < count; ++edge) {
            uint32_t next = edge == count - 1 ? 0 : edge + 1;
            int64_t x1 = points[edge].x;
            int64_t y1 = points[edge].y;
            int64_t x2 = points[next].x;
            int64_t y2 = points[next].y;

            if (((y1 << 1) <= scan_y2 && (y2 << 1) > scan_y2) ||
                ((y2 << 1) <= scan_y2 && (y1 << 1) > scan_y2)) {
                int64_t dy = y2 - y1;
                int64_t numerator = (scan_y2 - (y1 << 1)) * (x2 - x1) * 65536;
                int64_t denominator = dy << 1;

                intersections[intersections_count++] = (x1 << 16) + (numerator / denominator);
            }
        }
        sort_fixed_intersections(intersections, intersections_count);
        for (i = 0; i + 1 < intersections_count; i += 2) {
            int x_start = ceil_fixed_16(intersections[i]);
            int x_end = floor_fixed_16(intersections[i + 1]);
            int clipped_x0;
            int clipped_x1;

            if (x_end < x_start || x_end < 0 || x_start >= buffer->width) {
                continue;
            }
            clipped_x0 = x_start < 0 ? 0 : x_start;
            clipped_x1 = x_end >= buffer->width ? buffer->width - 1 : x_end;
            fill_rect_raw(buffer, clipped_x0, y, clipped_x1 - clipped_x0 + 1, 1, color, false);
            if (clipped_x0 < dirty_x0) {
                dirty_x0 = clipped_x0;
            }
            if (y < dirty_y0) {
                dirty_y0 = y;
            }
            if (clipped_x1 > dirty_x1) {
                dirty_x1 = clipped_x1;
            }
            if (y > dirty_y1) {
                dirty_y1 = y;
            }
        }
    }

    if (dirty_x1 >= dirty_x0 && dirty_y1 >= dirty_y0) {
        mark_dirty(buffer, dirty_x0, dirty_y0, dirty_x1 - dirty_x0 + 1, dirty_y1 - dirty_y0 + 1);
    }
    if (owns_intersections) {
        heap_caps_free(intersections);
    }
    return true;
}

static void draw_polyline_raw(esp32_mquickjs_display_buffer_t *buffer,
                              const display_buffer_point_t *points,
                              uint32_t count,
                              bool closed,
                              uint16_t color)
{
    int min_x;
    int min_y;
    int max_x;
    int max_y;
    uint32_t i;

    if (count < 2) {
        return;
    }
    min_x = points[0].x;
    min_y = points[0].y;
    max_x = points[0].x;
    max_y = points[0].y;
    for (i = 1; i < count; ++i) {
        draw_line_raw(buffer, points[i - 1].x, points[i - 1].y, points[i].x, points[i].y, color);
        if (points[i].x < min_x) {
            min_x = points[i].x;
        }
        if (points[i].y < min_y) {
            min_y = points[i].y;
        }
        if (points[i].x > max_x) {
            max_x = points[i].x;
        }
        if (points[i].y > max_y) {
            max_y = points[i].y;
        }
    }
    if (closed && count > 2) {
        draw_line_raw(buffer, points[count - 1].x, points[count - 1].y, points[0].x, points[0].y, color);
    }
    mark_dirty(buffer, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

static bool curve_segments_from_options(JSContext *ctx,
                                        JSValue options,
                                        uint32_t fallback,
                                        const char *api_name,
                                        uint32_t *out_segments)
{
    JSGCRef property_ref;
    JSValue *property;
    int32_t segments = (int32_t)fallback;

    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        *out_segments = fallback;
        return true;
    }
    if (JS_GetClassID(ctx, options) < 0) {
        JS_ThrowTypeError(ctx, "%s options must be an object", api_name);
        return false;
    }
    property = JS_PushGCRef(ctx, &property_ref);
    *property = JS_GetPropertyStr(ctx, options, "segments");
    if (JS_IsException(*property)) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    if (!JS_IsUndefined(*property) && !JS_IsNull(*property) && !value_to_i32(ctx, *property, &segments)) {
        JS_PopGCRef(ctx, &property_ref);
        JS_ThrowTypeError(ctx, "%s option 'segments' expects an integer", api_name);
        return false;
    }
    JS_PopGCRef(ctx, &property_ref);
    if (segments < 2) {
        segments = 2;
    }
    if (segments > 128) {
        segments = 128;
    }
    *out_segments = (uint32_t)segments;
    return true;
}

JSValue js_display_buffer_draw_circle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t cx;
    int32_t cy;
    int32_t radius;
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.drawCircle()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 3 || !value_to_i32(ctx, argv[0], &cx) || !value_to_i32(ctx, argv[1], &cy) ||
        !value_to_i32(ctx, argv[2], &radius)) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawCircle(cx, cy, radius, color) expects integer coordinates");
    }
    color = normalize_color(ctx, buffer->format, argc >= 4 ? argv[3] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawCircle(cx, cy, radius, color) expects a valid color");
    }
    draw_circle_raw(buffer, cx, cy, abs_i32_to_u32(radius), color);
    return *this_val;
}

JSValue js_display_buffer_fill_circle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t cx;
    int32_t cy;
    int32_t radius;
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.fillCircle()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 3 || !value_to_i32(ctx, argv[0], &cx) || !value_to_i32(ctx, argv[1], &cy) ||
        !value_to_i32(ctx, argv[2], &radius)) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.fillCircle(cx, cy, radius, color) expects integer coordinates");
    }
    color = normalize_color(ctx, buffer->format, argc >= 4 ? argv[3] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.fillCircle(cx, cy, radius, color) expects a valid color");
    }
    fill_circle_raw(buffer, cx, cy, abs_i32_to_u32(radius), color);
    return *this_val;
}

JSValue js_display_buffer_draw_ellipse(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t cx;
    int32_t cy;
    int32_t rx;
    int32_t ry;
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.drawEllipse()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 4 || !value_to_i32(ctx, argv[0], &cx) || !value_to_i32(ctx, argv[1], &cy) ||
        !value_to_i32(ctx, argv[2], &rx) || !value_to_i32(ctx, argv[3], &ry)) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawEllipse(cx, cy, rx, ry, color) expects integer coordinates");
    }
    color = normalize_color(ctx, buffer->format, argc >= 5 ? argv[4] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawEllipse(cx, cy, rx, ry, color) expects a valid color");
    }
    draw_ellipse_raw(buffer, cx, cy, abs_i32_to_u32(rx), abs_i32_to_u32(ry), color);
    return *this_val;
}

JSValue js_display_buffer_fill_ellipse(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t cx;
    int32_t cy;
    int32_t rx;
    int32_t ry;
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.fillEllipse()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 4 || !value_to_i32(ctx, argv[0], &cx) || !value_to_i32(ctx, argv[1], &cy) ||
        !value_to_i32(ctx, argv[2], &rx) || !value_to_i32(ctx, argv[3], &ry)) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.fillEllipse(cx, cy, rx, ry, color) expects integer coordinates");
    }
    color = normalize_color(ctx, buffer->format, argc >= 5 ? argv[4] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.fillEllipse(cx, cy, rx, ry, color) expects a valid color");
    }
    fill_ellipse_raw(buffer, cx, cy, abs_i32_to_u32(rx), abs_i32_to_u32(ry), color);
    return *this_val;
}

JSValue js_display_buffer_draw_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.drawRect()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (!rect_from_args(ctx, argc, argv, &x, &y, &width, &height, "DisplayBuffer.drawRect()")) {
        return JS_EXCEPTION;
    }
    if (width <= 0 || height <= 0) {
        return *this_val;
    }
    color = normalize_color(ctx, buffer->format, argc >= 5 ? argv[4] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawRect(x, y, width, height, color) expects a valid color");
    }
    draw_line_raw(buffer, x, y, x + width - 1, y, color);
    draw_line_raw(buffer, x, y + height - 1, x + width - 1, y + height - 1, color);
    draw_line_raw(buffer, x, y, x, y + height - 1, color);
    draw_line_raw(buffer, x + width - 1, y, x + width - 1, y + height - 1, color);
    mark_dirty(buffer, x, y, width, height);
    return *this_val;
}

JSValue js_display_buffer_draw_round_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t radius;
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.drawRoundRect()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 5 || !value_to_i32(ctx, argv[0], &x) || !value_to_i32(ctx, argv[1], &y) ||
        !value_to_i32(ctx, argv[2], &width) || !value_to_i32(ctx, argv[3], &height) ||
        !value_to_i32(ctx, argv[4], &radius)) {
        return JS_ThrowTypeError(ctx,
                                 "DisplayBuffer.drawRoundRect(x, y, width, height, radius, color) expects integer coordinates");
    }
    color = normalize_color(ctx, buffer->format, argc >= 6 ? argv[5] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawRoundRect(x, y, width, height, radius, color) expects a valid color");
    }
    draw_round_rect_raw(buffer, x, y, width, height, abs_i32_to_u32(radius), color);
    return *this_val;
}

JSValue js_display_buffer_fill_round_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t radius;
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.fillRoundRect()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 5 || !value_to_i32(ctx, argv[0], &x) || !value_to_i32(ctx, argv[1], &y) ||
        !value_to_i32(ctx, argv[2], &width) || !value_to_i32(ctx, argv[3], &height) ||
        !value_to_i32(ctx, argv[4], &radius)) {
        return JS_ThrowTypeError(ctx,
                                 "DisplayBuffer.fillRoundRect(x, y, width, height, radius, color) expects integer coordinates");
    }
    color = normalize_color(ctx, buffer->format, argc >= 6 ? argv[5] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.fillRoundRect(x, y, width, height, radius, color) expects a valid color");
    }
    fill_round_rect_raw(buffer, x, y, width, height, abs_i32_to_u32(radius), color);
    return *this_val;
}

JSValue js_display_buffer_draw_line(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    uint16_t color;
    bool ok;
    int min_x;
    int min_y;
    int max_x;
    int max_y;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.drawLine()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 4 || !value_to_i32(ctx, argv[0], &x0) || !value_to_i32(ctx, argv[1], &y0) ||
        !value_to_i32(ctx, argv[2], &x1) || !value_to_i32(ctx, argv[3], &y1)) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawLine(x0, y0, x1, y1, color) expects integer coordinates");
    }
    color = normalize_color(ctx, buffer->format, argc >= 5 ? argv[4] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawLine(x0, y0, x1, y1, color) expects a valid color");
    }
    draw_line_raw(buffer, x0, y0, x1, y1, color);
    min_x = x0 < x1 ? x0 : x1;
    min_y = y0 < y1 ? y0 : y1;
    max_x = x0 > x1 ? x0 : x1;
    max_y = y0 > y1 ? y0 : y1;
    mark_dirty(buffer, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
    return *this_val;
}

static JSValue draw_point_list(JSContext *ctx,
                               JSValue *this_val,
                               int argc,
                               JSValue *argv,
                               const char *api_name,
                               bool closed)
{
    esp32_mquickjs_display_buffer_t *buffer;
    display_buffer_point_t stack_points[DISPLAY_BUFFER_STACK_POINTS];
    display_buffer_point_t *points = NULL;
    uint32_t count = 0;
    uint16_t color;
    bool ok;
    bool owns_points = false;

    buffer = display_buffer_from_value(ctx, *this_val, api_name);
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 1 || !parse_point_list(ctx,
                                      argv[0],
                                      api_name,
                                      stack_points,
                                      DISPLAY_BUFFER_STACK_POINTS,
                                      &points,
                                      &count,
                                      &owns_points)) {
        return JS_EXCEPTION;
    }
    color = normalize_color(ctx, buffer->format, argc >= 2 ? argv[1] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        if (owns_points) {
            heap_caps_free(points);
        }
        return JS_ThrowTypeError(ctx, "%s expects a valid color", api_name);
    }
    draw_polyline_raw(buffer, points, count, closed, color);
    if (owns_points) {
        heap_caps_free(points);
    }
    return *this_val;
}

JSValue js_display_buffer_draw_polyline(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return draw_point_list(ctx, this_val, argc, argv, "DisplayBuffer.drawPolyline(points, color)", false);
}

JSValue js_display_buffer_draw_polygon(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return draw_point_list(ctx, this_val, argc, argv, "DisplayBuffer.drawPolygon(points, color)", true);
}

JSValue js_display_buffer_fill_polygon(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    display_buffer_point_t stack_points[DISPLAY_BUFFER_STACK_POINTS];
    display_buffer_point_t *points = NULL;
    int64_t stack_intersections[DISPLAY_BUFFER_STACK_POINTS];
    uint32_t count = 0;
    uint16_t color;
    bool ok;
    bool owns_points = false;
    bool filled;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.fillPolygon()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 1 || !parse_point_list(ctx,
                                      argv[0],
                                      "DisplayBuffer.fillPolygon(points, color)",
                                      stack_points,
                                      DISPLAY_BUFFER_STACK_POINTS,
                                      &points,
                                      &count,
                                      &owns_points)) {
        return JS_EXCEPTION;
    }
    color = normalize_color(ctx, buffer->format, argc >= 2 ? argv[1] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        if (owns_points) {
            heap_caps_free(points);
        }
        return JS_ThrowTypeError(ctx, "DisplayBuffer.fillPolygon(points, color) expects a valid color");
    }
    filled = fill_polygon_raw(buffer, points, count, color, stack_intersections, DISPLAY_BUFFER_STACK_POINTS);
    if (owns_points) {
        heap_caps_free(points);
    }
    if (!filled) {
        return JS_ThrowOutOfMemory(ctx);
    }
    return *this_val;
}

JSValue js_display_buffer_draw_triangle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    display_buffer_point_t points[3];
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.drawTriangle()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 6 ||
        !value_to_i32(ctx, argv[0], &points[0].x) ||
        !value_to_i32(ctx, argv[1], &points[0].y) ||
        !value_to_i32(ctx, argv[2], &points[1].x) ||
        !value_to_i32(ctx, argv[3], &points[1].y) ||
        !value_to_i32(ctx, argv[4], &points[2].x) ||
        !value_to_i32(ctx, argv[5], &points[2].y)) {
        return JS_ThrowTypeError(ctx,
                                 "DisplayBuffer.drawTriangle(x0, y0, x1, y1, x2, y2, color) expects integer coordinates");
    }
    color = normalize_color(ctx, buffer->format, argc >= 7 ? argv[6] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx,
                                 "DisplayBuffer.drawTriangle(x0, y0, x1, y1, x2, y2, color) expects a valid color");
    }
    draw_polyline_raw(buffer, points, 3, true, color);
    return *this_val;
}

JSValue js_display_buffer_fill_triangle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    display_buffer_point_t points[3];
    int64_t intersections[3];
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.fillTriangle()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 6 ||
        !value_to_i32(ctx, argv[0], &points[0].x) ||
        !value_to_i32(ctx, argv[1], &points[0].y) ||
        !value_to_i32(ctx, argv[2], &points[1].x) ||
        !value_to_i32(ctx, argv[3], &points[1].y) ||
        !value_to_i32(ctx, argv[4], &points[2].x) ||
        !value_to_i32(ctx, argv[5], &points[2].y)) {
        return JS_ThrowTypeError(ctx,
                                 "DisplayBuffer.fillTriangle(x0, y0, x1, y1, x2, y2, color) expects integer coordinates");
    }
    color = normalize_color(ctx, buffer->format, argc >= 7 ? argv[6] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx,
                                 "DisplayBuffer.fillTriangle(x0, y0, x1, y1, x2, y2, color) expects a valid color");
    }
    if (!fill_polygon_raw(buffer, points, 3, color, intersections, 3)) {
        return JS_ThrowOutOfMemory(ctx);
    }
    return *this_val;
}

JSValue js_display_buffer_draw_quadratic_bezier(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x0;
    int32_t y0;
    int32_t cx;
    int32_t cy;
    int32_t x1;
    int32_t y1;
    uint32_t segments;
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.drawQuadraticBezier()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 6 ||
        !value_to_i32(ctx, argv[0], &x0) ||
        !value_to_i32(ctx, argv[1], &y0) ||
        !value_to_i32(ctx, argv[2], &cx) ||
        !value_to_i32(ctx, argv[3], &cy) ||
        !value_to_i32(ctx, argv[4], &x1) ||
        !value_to_i32(ctx, argv[5], &y1)) {
        return JS_ThrowTypeError(ctx,
                                 "DisplayBuffer.drawQuadraticBezier(x0, y0, cx, cy, x1, y1, color, options) expects integer coordinates");
    }
    color = normalize_color(ctx, buffer->format, argc >= 7 ? argv[6] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx,
                                 "DisplayBuffer.drawQuadraticBezier(x0, y0, cx, cy, x1, y1, color, options) expects a valid color");
    }
    if (!curve_segments_from_options(ctx,
                                     argc >= 8 ? argv[7] : JS_UNDEFINED,
                                     24,
                                     "DisplayBuffer.drawQuadraticBezier()",
                                     &segments)) {
        return JS_EXCEPTION;
    }
    draw_quadratic_bezier_raw(buffer, x0, y0, cx, cy, x1, y1, color, segments);
    return *this_val;
}

JSValue js_display_buffer_draw_cubic_bezier(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x0;
    int32_t y0;
    int32_t c1x;
    int32_t c1y;
    int32_t c2x;
    int32_t c2y;
    int32_t x1;
    int32_t y1;
    uint32_t segments;
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.drawCubicBezier()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 8 ||
        !value_to_i32(ctx, argv[0], &x0) ||
        !value_to_i32(ctx, argv[1], &y0) ||
        !value_to_i32(ctx, argv[2], &c1x) ||
        !value_to_i32(ctx, argv[3], &c1y) ||
        !value_to_i32(ctx, argv[4], &c2x) ||
        !value_to_i32(ctx, argv[5], &c2y) ||
        !value_to_i32(ctx, argv[6], &x1) ||
        !value_to_i32(ctx, argv[7], &y1)) {
        return JS_ThrowTypeError(ctx,
                                 "DisplayBuffer.drawCubicBezier(x0, y0, c1x, c1y, c2x, c2y, x1, y1, color, options) expects integer coordinates");
    }
    color = normalize_color(ctx, buffer->format, argc >= 9 ? argv[8] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx,
                                 "DisplayBuffer.drawCubicBezier(x0, y0, c1x, c1y, c2x, c2y, x1, y1, color, options) expects a valid color");
    }
    if (!curve_segments_from_options(ctx,
                                     argc >= 10 ? argv[9] : JS_UNDEFINED,
                                     32,
                                     "DisplayBuffer.drawCubicBezier()",
                                     &segments)) {
        return JS_EXCEPTION;
    }
    draw_cubic_bezier_raw(buffer, x0, y0, c1x, c1y, c2x, c2y, x1, y1, color, segments);
    return *this_val;
}

JSValue js_display_buffer_draw_bitmap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x;
    int32_t y;
    uint32_t width = 0;
    uint32_t height = 0;
    JSGCRef property_ref;
    JSValue *property;
    JSValue pixels = JS_UNDEFINED;
    uint32_t index = 0;
    uint16_t color;
    uint16_t background;
    bool ok;
    bool has_background = false;
    uint32_t row;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.drawBitmap()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 3 || !value_to_i32(ctx, argv[0], &x) || !value_to_i32(ctx, argv[1], &y) ||
        JS_GetClassID(ctx, argv[2]) < 0) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawBitmap(x, y, bitmap, options?) expects a bitmap object");
    }
    property = JS_PushGCRef(ctx, &property_ref);
    *property = JS_GetPropertyStr(ctx, argv[2], "width");
    if (JS_IsException(*property) || !value_to_u32(ctx, *property, &width)) {
        JS_PopGCRef(ctx, &property_ref);
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawBitmap() bitmap.width must be numeric");
    }
    *property = JS_GetPropertyStr(ctx, argv[2], "height");
    if (JS_IsException(*property) || !value_to_u32(ctx, *property, &height)) {
        JS_PopGCRef(ctx, &property_ref);
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawBitmap() bitmap.height must be numeric");
    }
    pixels = JS_GetPropertyStr(ctx, argv[2], "pixels");
    if (JS_IsException(pixels)) {
        JS_PopGCRef(ctx, &property_ref);
        return JS_EXCEPTION;
    }
    color = buffer->foreground;
    background = buffer->background;
    if (argc >= 4 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3])) {
        if (JS_GetClassID(ctx, argv[3]) < 0) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "DisplayBuffer.drawBitmap() options must be an object");
        }
        *property = JS_GetPropertyStr(ctx, argv[3], "color");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        color = normalize_color(ctx, buffer->format, *property, buffer->foreground, &ok);
        if (!ok) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "DisplayBuffer.drawBitmap() option 'color' must be valid");
        }
        *property = JS_GetPropertyStr(ctx, argv[3], "background");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !JS_IsNull(*property)) {
            background = normalize_color(ctx, buffer->format, *property, buffer->background, &ok);
            if (!ok) {
                JS_PopGCRef(ctx, &property_ref);
                return JS_ThrowTypeError(ctx, "DisplayBuffer.drawBitmap() option 'background' must be valid or null");
            }
            has_background = true;
        }
    }
    for (row = 0; row < height; ++row) {
        uint32_t col;

        for (col = 0; col < width; ++col) {
            uint32_t mask_pixel = 0;

            *property = JS_GetPropertyUint32(ctx, pixels, index++);
            if (JS_IsException(*property)) {
                JS_PopGCRef(ctx, &property_ref);
                return JS_EXCEPTION;
            }
            if (!JS_IsUndefined(*property) && !JS_IsNull(*property) &&
                (!value_to_u32(ctx, *property, &mask_pixel) || mask_pixel != 0)) {
                set_pixel_raw(buffer, x + (int32_t)col, y + (int32_t)row, color);
            } else if (has_background) {
                set_pixel_raw(buffer, x + (int32_t)col, y + (int32_t)row, background);
            }
        }
    }
    JS_PopGCRef(ctx, &property_ref);
    mark_dirty(buffer, x, y, (int)width, (int)height);
    return *this_val;
}


#endif
