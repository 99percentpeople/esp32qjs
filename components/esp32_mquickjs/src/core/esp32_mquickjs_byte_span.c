#include "utils/esp32_mquickjs_byte_span.h"

#include <string.h>

void esp32_mquickjs_byte_span_clear(esp32_mquickjs_byte_span_t *span)
{
    if (span == NULL) {
        return;
    }
    memset(span, 0, sizeof(*span));
    span->owner = JS_UNDEFINED;
}

bool esp32_mquickjs_byte_span_source_next(JSContext *ctx,
                                          esp32_mquickjs_byte_span_source_t *source,
                                          esp32_mquickjs_byte_span_t *out)
{
    if (out != NULL) {
        esp32_mquickjs_byte_span_clear(out);
    }
    if (source == NULL || source->next == NULL || out == NULL) {
        return false;
    }
    return source->next(ctx, source->opaque, out);
}

void esp32_mquickjs_byte_span_source_close(JSContext *ctx,
                                           esp32_mquickjs_byte_span_source_t *source)
{
    if (source == NULL) {
        return;
    }
    if (source->close != NULL) {
        source->close(ctx, source->opaque);
    }
    source->opaque = NULL;
    source->next = NULL;
    source->close = NULL;
}
