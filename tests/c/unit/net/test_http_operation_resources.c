#include "esp32_mquickjs_http_operation_resources.h"

#include <assert.h>
#include <stdlib.h>

typedef struct {
    size_t create_calls;
    size_t fail_at;
    void *created[2];
    void *released[2];
    size_t released_count;
} fixture_t;

static void *fixture_allocate(size_t size, void *opaque)
{
    fixture_t *fixture = opaque;
    void *value;

    fixture->create_calls++;
    if (fixture->create_calls == fixture->fail_at) {
        return NULL;
    }
    value = calloc(1, size);
    fixture->created[fixture->create_calls - 1U] = value;
    return value;
}

static void fixture_release(void *value, void *opaque)
{
    fixture_t *fixture = opaque;

    fixture->released[fixture->released_count++] = value;
    free(value);
}

static void *fixture_create_lock(void *opaque)
{
    return fixture_allocate(1, opaque);
}

static void fixture_delete_lock(void *lock, void *opaque)
{
    fixture_release(lock, opaque);
}

static esp32_mquickjs_http_operation_resource_ops_t fixture_ops(
    fixture_t *fixture)
{
    return (esp32_mquickjs_http_operation_resource_ops_t){
        .allocate = fixture_allocate,
        .release = fixture_release,
        .create_lock = fixture_create_lock,
        .delete_lock = fixture_delete_lock,
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

static void test_each_creation_failure_rolls_back(void)
{
    size_t fail_at;

    for (fail_at = 1; fail_at <= 2; ++fail_at) {
        fixture_t fixture = {.fail_at = fail_at};
        esp32_mquickjs_http_operation_resources_t resources = {0};
        esp32_mquickjs_http_operation_resource_ops_t ops =
            fixture_ops(&fixture);

        assert(!esp32_mquickjs_http_operation_resources_init(
            &resources, &ops, 64));
        assert(fixture.create_calls == fail_at);
        assert_reverse_release(&fixture, fail_at - 1U);
        assert(resources.operation == NULL);
        assert(resources.lock == NULL);
    }
}

static void test_success_deinit_is_reverse_and_idempotent(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_http_operation_resources_t resources = {0};
    esp32_mquickjs_http_operation_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_http_operation_resources_init(
        &resources, &ops, 64));
    assert(fixture.create_calls == 2);
    assert(fixture.released_count == 0);

    esp32_mquickjs_http_operation_resources_deinit(&resources, &ops);
    assert_reverse_release(&fixture, 2);
    esp32_mquickjs_http_operation_resources_deinit(&resources, &ops);
    assert(fixture.released_count == 2);
}

static void test_invalid_configuration_creates_nothing(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_http_operation_resources_t resources = {0};
    esp32_mquickjs_http_operation_resource_ops_t ops = fixture_ops(&fixture);

    assert(!esp32_mquickjs_http_operation_resources_init(
        &resources, &ops, 0));
    assert(fixture.create_calls == 0);
}

int main(void)
{
    test_each_creation_failure_rolls_back();
    test_success_deinit_is_reverse_and_idempotent();
    test_invalid_configuration_creates_nothing();
    return 0;
}
