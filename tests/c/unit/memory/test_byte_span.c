#include "utils/esp32_mquickjs_byte_span.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
    JSValue owner;
    int close_count;
} fake_span_source_t;

static void expect_true(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void expect_size(size_t actual, size_t expected, const char *message)
{
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %zu, got %zu)\n", message, expected, actual);
        exit(1);
    }
}

static bool fake_next(JSContext *ctx, void *opaque, esp32_mquickjs_byte_span_t *out)
{
    fake_span_source_t *source = opaque;
    size_t remaining;
    size_t length;

    (void)ctx;

    if (source->offset >= source->length) {
        return false;
    }
    remaining = source->length - source->offset;
    length = remaining > 3U ? 3U : remaining;
    out->data = source->data + source->offset;
    out->length = length;
    out->owner = source->owner;
    out->dma_capable = true;
    source->offset += length;
    return true;
}

static void fake_close(JSContext *ctx, void *opaque)
{
    fake_span_source_t *source = opaque;

    (void)ctx;
    source->close_count++;
}

static void test_span_iteration(void)
{
    const uint8_t data[] = {1, 2, 3, 4, 5, 6, 7};
    fake_span_source_t fake = {
        .data = data,
        .length = sizeof(data),
        .owner = JS_TRUE,
    };
    esp32_mquickjs_byte_span_source_t source = {
        .opaque = &fake,
        .next = fake_next,
        .close = fake_close,
    };
    esp32_mquickjs_byte_span_t span;

    expect_true(esp32_mquickjs_byte_span_source_next(NULL, &source, &span),
                "first span should be available");
    expect_size(span.length, 3, "first span length");
    expect_true(span.data == data, "first span pointer");
    expect_true(span.owner == JS_TRUE, "span should preserve owner token");
    expect_true(span.dma_capable, "span should preserve DMA hint");

    expect_true(esp32_mquickjs_byte_span_source_next(NULL, &source, &span),
                "second span should be available");
    expect_size(span.length, 3, "second span length");
    expect_true(span.data == data + 3, "second span pointer");

    expect_true(esp32_mquickjs_byte_span_source_next(NULL, &source, &span),
                "third span should be available");
    expect_size(span.length, 1, "third span length");
    expect_true(span.data == data + 6, "third span pointer");

    expect_true(!esp32_mquickjs_byte_span_source_next(NULL, &source, &span),
                "source should be exhausted");
    expect_true(span.owner == JS_UNDEFINED, "exhausted span should clear owner");

    esp32_mquickjs_byte_span_source_close(NULL, &source);
    expect_size((size_t)fake.close_count, 1, "close should run once");
    expect_true(source.next == NULL, "close should clear next callback");
}

int main(void)
{
    test_span_iteration();
    return 0;
}
