#include "esp32_mquickjs_i2s_timer_resources.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FIXTURE_RX_ERROR = 101,
    FIXTURE_TX_ERROR = 102,
};

typedef struct {
    bool fail_rx;
    bool fail_tx;
    size_t create_calls;
    void *created[2];
    void *deleted[2];
    size_t deleted_count;
} fixture_t;

static int fixture_create(fixture_t *fixture, bool rx, void **out_timer)
{
    void *timer;

    fixture->create_calls++;
    if ((rx && fixture->fail_rx) || (!rx && fixture->fail_tx)) {
        return rx ? FIXTURE_RX_ERROR : FIXTURE_TX_ERROR;
    }
    timer = (void *)(uintptr_t)(fixture->create_calls + 16U);
    fixture->created[fixture->create_calls - 1U] = timer;
    *out_timer = timer;
    return 0;
}

static int fixture_create_rx(void *opaque, void **out_timer)
{
    return fixture_create(opaque, true, out_timer);
}

static int fixture_create_tx(void *opaque, void **out_timer)
{
    return fixture_create(opaque, false, out_timer);
}

static void fixture_delete(void *timer, void *opaque)
{
    fixture_t *fixture = opaque;

    fixture->deleted[fixture->deleted_count++] = timer;
}

static esp32_mquickjs_i2s_timer_resource_ops_t fixture_ops(
    fixture_t *fixture)
{
    return (esp32_mquickjs_i2s_timer_resource_ops_t){
        .create_rx = fixture_create_rx,
        .create_tx = fixture_create_tx,
        .delete_timer = fixture_delete,
        .opaque = fixture,
    };
}

static void test_each_creation_failure_rolls_back(void)
{
    fixture_t rx_fixture = {.fail_rx = true};
    fixture_t tx_fixture = {.fail_tx = true};
    esp32_mquickjs_i2s_timer_resources_t resources = {0};
    esp32_mquickjs_i2s_timer_resource_ops_t ops = fixture_ops(&rx_fixture);

    assert(esp32_mquickjs_i2s_timer_resources_init(
               &resources, &ops, true, true) == FIXTURE_RX_ERROR);
    assert(rx_fixture.create_calls == 1);
    assert(rx_fixture.deleted_count == 0);
    assert(resources.rx_timer == NULL);
    assert(resources.tx_timer == NULL);

    ops = fixture_ops(&tx_fixture);
    assert(esp32_mquickjs_i2s_timer_resources_init(
               &resources, &ops, true, true) == FIXTURE_TX_ERROR);
    assert(tx_fixture.create_calls == 2);
    assert(tx_fixture.deleted_count == 1);
    assert(tx_fixture.deleted[0] == tx_fixture.created[0]);
    assert(resources.rx_timer == NULL);
    assert(resources.tx_timer == NULL);
}

static void test_success_deinit_is_reverse_and_idempotent(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_i2s_timer_resources_t resources = {0};
    esp32_mquickjs_i2s_timer_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_i2s_timer_resources_init(
               &resources, &ops, true, true) == 0);
    assert(fixture.create_calls == 2);
    esp32_mquickjs_i2s_timer_resources_deinit(&resources, &ops);
    assert(fixture.deleted_count == 2);
    assert(fixture.deleted[0] == fixture.created[1]);
    assert(fixture.deleted[1] == fixture.created[0]);
    esp32_mquickjs_i2s_timer_resources_deinit(&resources, &ops);
    assert(fixture.deleted_count == 2);
}

static void test_each_direction_and_no_timer_are_supported(void)
{
    fixture_t rx_fixture = {0};
    fixture_t tx_fixture = {0};
    fixture_t none_fixture = {0};
    esp32_mquickjs_i2s_timer_resources_t resources = {0};
    esp32_mquickjs_i2s_timer_resource_ops_t ops = fixture_ops(&rx_fixture);

    assert(esp32_mquickjs_i2s_timer_resources_init(
               &resources, &ops, true, false) == 0);
    assert(resources.rx_timer != NULL);
    assert(resources.tx_timer == NULL);
    esp32_mquickjs_i2s_timer_resources_deinit(&resources, &ops);

    ops = fixture_ops(&tx_fixture);
    assert(esp32_mquickjs_i2s_timer_resources_init(
               &resources, &ops, false, true) == 0);
    assert(resources.rx_timer == NULL);
    assert(resources.tx_timer != NULL);
    esp32_mquickjs_i2s_timer_resources_deinit(&resources, &ops);

    ops = fixture_ops(&none_fixture);
    assert(esp32_mquickjs_i2s_timer_resources_init(
               &resources, &ops, false, false) == 0);
    assert(none_fixture.create_calls == 0);
}

int main(void)
{
    test_each_creation_failure_rolls_back();
    test_success_deinit_is_reverse_and_idempotent();
    test_each_direction_and_no_timer_are_supported();
    return 0;
}
