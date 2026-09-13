#include "esp32_mquickjs_rmt_symbol_buffer_resources.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    size_t allocation_calls;
    size_t fail_at;
    void *allocated[2];
    void *released[2];
    size_t released_count;
} fixture_t;

static void *fixture_allocate(size_t size, void *opaque)
{
    fixture_t *fixture = opaque;
    void *value;

    fixture->allocation_calls++;
    if (fixture->allocation_calls == fixture->fail_at) {
        return NULL;
    }
    value = calloc(1, size);
    fixture->allocated[fixture->allocation_calls - 1U] = value;
    return value;
}

static void fixture_release(void *value, void *opaque)
{
    fixture_t *fixture = opaque;

    fixture->released[fixture->released_count++] = value;
    free(value);
}

static esp32_mquickjs_rmt_symbol_buffer_resource_ops_t fixture_ops(
    fixture_t *fixture)
{
    return (esp32_mquickjs_rmt_symbol_buffer_resource_ops_t){
        .allocate_buffer = fixture_allocate,
        .allocate_symbols = fixture_allocate,
        .release = fixture_release,
        .opaque = fixture,
    };
}

static void assert_reverse_release(const fixture_t *fixture,
                                   size_t allocation_count)
{
    size_t index;

    assert(fixture->released_count == allocation_count);
    for (index = 0; index < allocation_count; ++index) {
        assert(fixture->released[index] ==
               fixture->allocated[allocation_count - index - 1U]);
    }
}

static void test_each_allocation_failure_rolls_back(void)
{
    size_t fail_at;

    for (fail_at = 1; fail_at <= 2; ++fail_at) {
        fixture_t fixture = {.fail_at = fail_at};
        esp32_mquickjs_rmt_symbol_buffer_resources_t resources = {0};
        esp32_mquickjs_rmt_symbol_buffer_resource_ops_t ops =
            fixture_ops(&fixture);

        assert(!esp32_mquickjs_rmt_symbol_buffer_resources_init(
            &resources, &ops, 64, 8, sizeof(uint32_t)));
        assert(fixture.allocation_calls == fail_at);
        assert_reverse_release(&fixture, fail_at - 1U);
        assert(resources.buffer == NULL);
        assert(resources.symbols == NULL);
    }
}

static void test_success_deinit_is_reverse_and_idempotent(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_rmt_symbol_buffer_resources_t resources = {0};
    esp32_mquickjs_rmt_symbol_buffer_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_rmt_symbol_buffer_resources_init(
        &resources, &ops, 64, 8, sizeof(uint32_t)));
    assert(fixture.allocation_calls == 2);
    assert(fixture.released_count == 0);

    esp32_mquickjs_rmt_symbol_buffer_resources_deinit(&resources, &ops);
    assert_reverse_release(&fixture, 2);
    esp32_mquickjs_rmt_symbol_buffer_resources_deinit(&resources, &ops);
    assert(fixture.released_count == 2);
}

static void test_invalid_sizes_allocate_nothing(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_rmt_symbol_buffer_resources_t resources = {0};
    esp32_mquickjs_rmt_symbol_buffer_resource_ops_t ops = fixture_ops(&fixture);

    assert(!esp32_mquickjs_rmt_symbol_buffer_resources_init(
        &resources, &ops, 0, 8, sizeof(uint32_t)));
    assert(!esp32_mquickjs_rmt_symbol_buffer_resources_init(
        &resources, &ops, 64, 0, sizeof(uint32_t)));
    assert(!esp32_mquickjs_rmt_symbol_buffer_resources_init(
        &resources, &ops, 64, SIZE_MAX, 2));
    assert(fixture.allocation_calls == 0);
}

int main(void)
{
    test_each_allocation_failure_rolls_back();
    test_success_deinit_is_reverse_and_idempotent();
    test_invalid_sizes_allocate_nothing();
    return 0;
}
