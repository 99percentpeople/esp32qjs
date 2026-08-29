#include "esp32_mquickjs_future_runtime_resources.h"

#include <assert.h>
#include <stdlib.h>

typedef struct {
    size_t allocation_calls;
    size_t fail_at;
    void *created[4];
    void *released[4];
    size_t released_count;
} fixture_t;

static void *fixture_allocate(size_t count, size_t size, void *opaque)
{
    fixture_t *fixture = opaque;
    void *value;

    fixture->allocation_calls++;
    if (fixture->allocation_calls == fixture->fail_at) {
        return NULL;
    }
    value = calloc(count, size);
    fixture->created[fixture->allocation_calls - 1U] = value;
    return value;
}

static void fixture_free(void *value, void *opaque)
{
    fixture_t *fixture = opaque;

    fixture->released[fixture->released_count++] = value;
    free(value);
}

static void *fixture_queue_create(size_t length,
                                  size_t item_size,
                                  void *opaque)
{
    return fixture_allocate(length, item_size, opaque);
}

static void fixture_queue_delete(void *queue, void *opaque)
{
    fixture_free(queue, opaque);
}

static esp32_mquickjs_future_runtime_resource_ops_t fixture_ops(
    fixture_t *fixture)
{
    return (esp32_mquickjs_future_runtime_resource_ops_t){
        .allocate = fixture_allocate,
        .release = fixture_free,
        .queue_create = fixture_queue_create,
        .queue_delete = fixture_queue_delete,
        .opaque = fixture,
    };
}

static void assert_reverse_release(const fixture_t *fixture,
                                   size_t created_count)
{
    size_t index;

    assert(fixture->released_count == created_count);
    for (index = 0; index < created_count; ++index) {
        assert(fixture->released[index] ==
               fixture->created[created_count - index - 1U]);
    }
}

static void test_each_creation_failure_rolls_back_in_reverse(void)
{
    size_t fail_at;

    for (fail_at = 1; fail_at <= 4; ++fail_at) {
        fixture_t fixture = {.fail_at = fail_at};
        esp32_mquickjs_future_runtime_resources_t resources = {0};
        esp32_mquickjs_future_runtime_resource_ops_t ops =
            fixture_ops(&fixture);

        assert(!esp32_mquickjs_future_runtime_resources_init(
            &resources, &ops, 64, 8, 32, 6, 8));
        assert(fixture.allocation_calls == fail_at);
        assert_reverse_release(&fixture, fail_at - 1U);
        assert(resources.runtime_state == NULL);
        assert(resources.slots == NULL);
        assert(resources.submissions == NULL);
        assert(resources.ready == NULL);
    }
}

static void test_successful_deinit_is_reverse_and_idempotent(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_future_runtime_resources_t resources = {0};
    esp32_mquickjs_future_runtime_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_future_runtime_resources_init(
        &resources, &ops, 64, 8, 32, 6, 8));
    assert(fixture.allocation_calls == 4);
    assert(fixture.released_count == 0);

    esp32_mquickjs_future_runtime_resources_deinit(&resources, &ops);
    assert_reverse_release(&fixture, 4);
    esp32_mquickjs_future_runtime_resources_deinit(&resources, &ops);
    assert(fixture.released_count == 4);
}

static void test_invalid_configuration_creates_nothing(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_future_runtime_resources_t resources = {0};
    esp32_mquickjs_future_runtime_resource_ops_t ops = fixture_ops(&fixture);

    assert(!esp32_mquickjs_future_runtime_resources_init(
        &resources, &ops, 0, 8, 32, 6, 8));
    assert(fixture.allocation_calls == 0);
    assert(!esp32_mquickjs_future_runtime_resources_init(
        &resources, &ops, 64, 0, 32, 6, 8));
    assert(fixture.allocation_calls == 0);
}

int main(void)
{
    test_each_creation_failure_rolls_back_in_reverse();
    test_successful_deinit_is_reverse_and_idempotent();
    test_invalid_configuration_creates_nothing();
    return 0;
}
