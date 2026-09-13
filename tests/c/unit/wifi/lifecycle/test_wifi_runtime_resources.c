#include "esp32_mquickjs_wifi_runtime_resources.h"

#include <assert.h>
#include <stdlib.h>

typedef struct {
    size_t create_calls;
    size_t fail_at;
    void *created[5];
    void *deleted[5];
    size_t deleted_count;
} fixture_t;

static void *fixture_create(void *opaque)
{
    fixture_t *fixture = opaque;
    void *value;

    fixture->create_calls++;
    if (fixture->create_calls == fixture->fail_at) {
        return NULL;
    }
    value = calloc(1, 1);
    fixture->created[fixture->create_calls - 1U] = value;
    return value;
}

static void *fixture_create_queue(size_t length, size_t item_size, void *opaque)
{
    assert(length > 0);
    assert(item_size > 0);
    return fixture_create(opaque);
}

static void fixture_delete(void *value, void *opaque)
{
    fixture_t *fixture = opaque;

    fixture->deleted[fixture->deleted_count++] = value;
    free(value);
}

static esp32_mquickjs_wifi_runtime_resource_ops_t fixture_ops(
    fixture_t *fixture)
{
    return (esp32_mquickjs_wifi_runtime_resource_ops_t){
        .create_lock = fixture_create,
        .delete_lock = fixture_delete,
        .create_event_group = fixture_create,
        .delete_event_group = fixture_delete,
        .create_queue = fixture_create_queue,
        .delete_queue = fixture_delete,
        .opaque = fixture,
    };
}

static void assert_reverse_delete(const fixture_t *fixture,
                                  size_t created_count)
{
    size_t index;

    assert(fixture->deleted_count == created_count);
    for (index = 0; index < created_count; ++index) {
        assert(fixture->deleted[index] ==
               fixture->created[created_count - index - 1U]);
    }
}

static void test_each_creation_failure_rolls_back(void)
{
    size_t fail_at;

    for (fail_at = 1; fail_at <= 5; ++fail_at) {
        fixture_t fixture = {.fail_at = fail_at};
        esp32_mquickjs_wifi_runtime_resources_t resources = {0};
        esp32_mquickjs_wifi_runtime_resource_ops_t ops = fixture_ops(&fixture);

        assert(!esp32_mquickjs_wifi_runtime_resources_init(
            &resources, &ops, 1, 8, 1, 12, 8, 16));
        assert(fixture.create_calls == fail_at);
        assert_reverse_delete(&fixture, fail_at - 1U);
        assert(resources.lock == NULL);
        assert(resources.event_group == NULL);
        assert(resources.scan_queue == NULL);
        assert(resources.connect_queue == NULL);
        assert(resources.driver_event_queue == NULL);
    }
}

static void test_success_deinit_is_reverse_and_idempotent(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_wifi_runtime_resources_t resources = {0};
    esp32_mquickjs_wifi_runtime_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_wifi_runtime_resources_init(
        &resources, &ops, 1, 8, 1, 12, 8, 16));
    assert(fixture.create_calls == 5);
    assert(fixture.deleted_count == 0);

    esp32_mquickjs_wifi_runtime_resources_deinit(&resources, &ops);
    assert_reverse_delete(&fixture, 5);
    esp32_mquickjs_wifi_runtime_resources_deinit(&resources, &ops);
    assert(fixture.deleted_count == 5);
}

static void test_invalid_queue_sizes_create_nothing(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_wifi_runtime_resources_t resources = {0};
    esp32_mquickjs_wifi_runtime_resource_ops_t ops = fixture_ops(&fixture);

    assert(!esp32_mquickjs_wifi_runtime_resources_init(
        &resources, &ops, 0, 8, 1, 12, 8, 16));
    assert(!esp32_mquickjs_wifi_runtime_resources_init(
        &resources, &ops, 1, 0, 1, 12, 8, 16));
    assert(!esp32_mquickjs_wifi_runtime_resources_init(
        &resources, &ops, 1, 8, 0, 12, 8, 16));
    assert(!esp32_mquickjs_wifi_runtime_resources_init(
        &resources, &ops, 1, 8, 1, 0, 8, 16));
    assert(!esp32_mquickjs_wifi_runtime_resources_init(
        &resources, &ops, 1, 8, 1, 12, 0, 16));
    assert(!esp32_mquickjs_wifi_runtime_resources_init(
        &resources, &ops, 1, 8, 1, 12, 8, 0));
    assert(fixture.create_calls == 0);
}

int main(void)
{
    test_each_creation_failure_rolls_back();
    test_success_deinit_is_reverse_and_idempotent();
    test_invalid_queue_sizes_create_nothing();
    return 0;
}
