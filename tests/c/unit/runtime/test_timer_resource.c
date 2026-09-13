#include "esp32_mquickjs_timer_resource.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    FIXTURE_CREATE_ERROR = 301,
    FIXTURE_START_ERROR = 302,
};

typedef struct {
    bool fail_create;
    bool omit_timer;
    bool fail_start;
    size_t create_calls;
    size_t start_calls;
    size_t stop_calls;
    size_t delete_calls;
    char log[8];
    size_t log_length;
} fixture_t;

static int fixture_create(void *opaque, void **out_timer)
{
    fixture_t *fixture = opaque;

    fixture->log[fixture->log_length++] = 'C';
    fixture->create_calls++;
    if (fixture->fail_create) {
        return FIXTURE_CREATE_ERROR;
    }
    if (!fixture->omit_timer) {
        *out_timer = (void *)(uintptr_t)0x31U;
    }
    return 0;
}

static int fixture_start(void *timer, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(timer == (void *)(uintptr_t)0x31U);
    fixture->log[fixture->log_length++] = 'S';
    fixture->start_calls++;
    return fixture->fail_start ? FIXTURE_START_ERROR : 0;
}

static void fixture_stop(void *timer, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(timer == (void *)(uintptr_t)0x31U);
    fixture->log[fixture->log_length++] = 'T';
    fixture->stop_calls++;
}

static void fixture_delete(void *timer, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(timer == (void *)(uintptr_t)0x31U);
    fixture->log[fixture->log_length++] = 'D';
    fixture->delete_calls++;
}

static esp32_mquickjs_timer_resource_ops_t fixture_ops(fixture_t *fixture)
{
    return (esp32_mquickjs_timer_resource_ops_t){
        .create = fixture_create,
        .start = fixture_start,
        .stop = fixture_stop,
        .delete_timer = fixture_delete,
        .opaque = fixture,
    };
}

static void test_create_failure_stops_without_cleanup(void)
{
    fixture_t fixture = {.fail_create = true};
    esp32_mquickjs_timer_resource_t resource = {0};
    esp32_mquickjs_timer_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_timer_resource_init(&resource, &ops) ==
           FIXTURE_CREATE_ERROR);
    assert(strcmp(fixture.log, "C") == 0);
    assert(resource.timer == NULL);
    assert(!resource.started);
}

static void test_missing_created_timer_is_rejected(void)
{
    fixture_t fixture = {.omit_timer = true};
    esp32_mquickjs_timer_resource_t resource = {0};
    esp32_mquickjs_timer_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_timer_resource_init(&resource, &ops) != 0);
    assert(strcmp(fixture.log, "C") == 0);
    assert(resource.timer == NULL);
    assert(!resource.started);
}

static void test_start_failure_deletes_without_stopping(void)
{
    fixture_t fixture = {.fail_start = true};
    esp32_mquickjs_timer_resource_t resource = {0};
    esp32_mquickjs_timer_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_timer_resource_init(&resource, &ops) ==
           FIXTURE_START_ERROR);
    assert(strcmp(fixture.log, "CSD") == 0);
    assert(fixture.stop_calls == 0);
    assert(fixture.delete_calls == 1);
    assert(resource.timer == NULL);
    assert(!resource.started);
}

static void test_success_stops_then_deletes_once(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_timer_resource_t resource = {0};
    esp32_mquickjs_timer_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_timer_resource_init(&resource, &ops) == 0);
    assert(resource.timer == (void *)(uintptr_t)0x31U);
    assert(resource.started);
    esp32_mquickjs_timer_resource_deinit(&resource, &ops);
    assert(strcmp(fixture.log, "CSTD") == 0);
    assert(fixture.stop_calls == 1);
    assert(fixture.delete_calls == 1);
    assert(resource.timer == NULL);
    assert(!resource.started);
    esp32_mquickjs_timer_resource_deinit(&resource, &ops);
    assert(fixture.stop_calls == 1);
    assert(fixture.delete_calls == 1);
}

static void test_acquired_timer_can_be_started_and_stopped_repeatedly(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_timer_resource_t resource = {0};
    esp32_mquickjs_timer_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_timer_resource_acquire(&resource, &ops) == 0);
    assert(strcmp(fixture.log, "C") == 0);
    assert(resource.timer == (void *)(uintptr_t)0x31U);
    assert(!resource.started);

    assert(esp32_mquickjs_timer_resource_start(&resource, &ops) == 0);
    assert(resource.started);
    esp32_mquickjs_timer_resource_stop(&resource, &ops);
    assert(!resource.started);
    esp32_mquickjs_timer_resource_stop(&resource, &ops);
    assert(strcmp(fixture.log, "CST") == 0);

    assert(esp32_mquickjs_timer_resource_start(&resource, &ops) == 0);
    esp32_mquickjs_timer_resource_deinit(&resource, &ops);
    assert(strcmp(fixture.log, "CSTSTD") == 0);
    assert(resource.timer == NULL);
    assert(!resource.started);
}

static void test_explicit_start_failure_keeps_unstarted_timer_owned(void)
{
    fixture_t fixture = {.fail_start = true};
    esp32_mquickjs_timer_resource_t resource = {0};
    esp32_mquickjs_timer_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_timer_resource_acquire(&resource, &ops) == 0);
    assert(esp32_mquickjs_timer_resource_start(&resource, &ops) ==
           FIXTURE_START_ERROR);
    assert(resource.timer == (void *)(uintptr_t)0x31U);
    assert(!resource.started);
    assert(strcmp(fixture.log, "CS") == 0);

    esp32_mquickjs_timer_resource_deinit(&resource, &ops);
    assert(strcmp(fixture.log, "CSD") == 0);
    assert(fixture.stop_calls == 0);
    assert(fixture.delete_calls == 1);
}

int main(void)
{
    test_create_failure_stops_without_cleanup();
    test_missing_created_timer_is_rejected();
    test_start_failure_deletes_without_stopping();
    test_success_stops_then_deletes_once();
    test_acquired_timer_can_be_started_and_stopped_repeatedly();
    test_explicit_start_failure_keeps_unstarted_timer_owned();
    return 0;
}
