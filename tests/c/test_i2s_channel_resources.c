#include "esp32_mquickjs_i2s_channel_resources.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    FIXTURE_CREATE_ERROR = 601,
    FIXTURE_ENABLE_TX_ERROR = 602,
    FIXTURE_ENABLE_RX_ERROR = 603,
    FIXTURE_DISABLE_TX_ERROR = 604,
    FIXTURE_DISABLE_RX_ERROR = 605,
    FIXTURE_DELETE_TX_ERROR = 606,
    FIXTURE_DELETE_RX_ERROR = 607,
};

static void *const FIXTURE_TX = (void *)(uintptr_t)0x11U;
static void *const FIXTURE_RX = (void *)(uintptr_t)0x22U;

typedef struct {
    bool need_tx;
    bool need_rx;
    bool fail_create;
    bool omit_tx;
    bool omit_rx;
    bool fail_enable_tx;
    bool fail_enable_rx;
    bool fail_disable_tx;
    bool fail_disable_rx;
    bool fail_delete_tx;
    bool fail_delete_rx;
    char log[48];
    size_t log_length;
} fixture_t;

static int fixture_create(void *opaque, void **out_tx, void **out_rx)
{
    fixture_t *fixture = opaque;

    fixture->log[fixture->log_length++] = 'C';
    assert((out_tx != NULL) == fixture->need_tx);
    assert((out_rx != NULL) == fixture->need_rx);
    if (out_tx != NULL && !fixture->omit_tx) {
        *out_tx = FIXTURE_TX;
    }
    if (out_rx != NULL && !fixture->omit_rx) {
        *out_rx = FIXTURE_RX;
    }
    return fixture->fail_create ? FIXTURE_CREATE_ERROR : 0;
}

static int fixture_enable(void *channel, void *opaque)
{
    fixture_t *fixture = opaque;

    if (channel == FIXTURE_TX) {
        fixture->log[fixture->log_length++] = 'T';
        return fixture->fail_enable_tx ? FIXTURE_ENABLE_TX_ERROR : 0;
    }
    assert(channel == FIXTURE_RX);
    fixture->log[fixture->log_length++] = 'R';
    return fixture->fail_enable_rx ? FIXTURE_ENABLE_RX_ERROR : 0;
}

static int fixture_disable(void *channel, void *opaque)
{
    fixture_t *fixture = opaque;

    if (channel == FIXTURE_RX) {
        fixture->log[fixture->log_length++] = 'r';
        return fixture->fail_disable_rx ? FIXTURE_DISABLE_RX_ERROR : 0;
    }
    assert(channel == FIXTURE_TX);
    fixture->log[fixture->log_length++] = 't';
    return fixture->fail_disable_tx ? FIXTURE_DISABLE_TX_ERROR : 0;
}

static int fixture_delete(void *channel, void *opaque)
{
    fixture_t *fixture = opaque;

    if (channel == FIXTURE_RX) {
        fixture->log[fixture->log_length++] = 'x';
        return fixture->fail_delete_rx ? FIXTURE_DELETE_RX_ERROR : 0;
    }
    assert(channel == FIXTURE_TX);
    fixture->log[fixture->log_length++] = 'y';
    return fixture->fail_delete_tx ? FIXTURE_DELETE_TX_ERROR : 0;
}

static esp32_mquickjs_i2s_channel_resource_ops_t fixture_ops(
    fixture_t *fixture)
{
    return (esp32_mquickjs_i2s_channel_resource_ops_t){
        .create_channels = fixture_create,
        .enable_channel = fixture_enable,
        .disable_channel = fixture_disable,
        .delete_channel = fixture_delete,
        .opaque = fixture,
    };
}

static void test_creation_failure_rolls_back_partial_handles(void)
{
    fixture_t fixture = {
        .need_tx = true,
        .need_rx = true,
        .fail_create = true,
    };
    esp32_mquickjs_i2s_channel_resources_t resources = {0};
    esp32_mquickjs_i2s_channel_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_i2s_channel_resources_init(
               &resources, true, true, &ops) == FIXTURE_CREATE_ERROR);
    assert(strcmp(fixture.log, "Cxy") == 0);
    assert(resources.rx_channel == NULL);
    assert(resources.tx_channel == NULL);
}

static void test_missing_required_handle_is_rejected(void)
{
    fixture_t fixture = {
        .need_tx = true,
        .need_rx = true,
        .omit_rx = true,
    };
    esp32_mquickjs_i2s_channel_resources_t resources = {0};
    esp32_mquickjs_i2s_channel_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_i2s_channel_resources_init(
               &resources, true, true, &ops) != 0);
    assert(strcmp(fixture.log, "Cy") == 0);
    assert(resources.rx_channel == NULL);
    assert(resources.tx_channel == NULL);
}

static void test_duplex_start_stop_delete_are_ordered_and_idempotent(void)
{
    fixture_t fixture = {.need_tx = true, .need_rx = true};
    esp32_mquickjs_i2s_channel_resources_t resources = {0};
    esp32_mquickjs_i2s_channel_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_i2s_channel_resources_init(
               &resources, true, true, &ops) == 0);
    assert(esp32_mquickjs_i2s_channel_resources_start(
               &resources, &ops) == 0);
    assert(resources.tx_enabled);
    assert(resources.rx_enabled);
    assert(esp32_mquickjs_i2s_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "CTRrtxy") == 0);
    assert(resources.rx_channel == NULL);
    assert(resources.tx_channel == NULL);
    assert(!resources.rx_enabled);
    assert(!resources.tx_enabled);
    assert(esp32_mquickjs_i2s_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "CTRrtxy") == 0);
}

static void test_rx_start_failure_rolls_back_new_tx_enable(void)
{
    fixture_t fixture = {
        .need_tx = true,
        .need_rx = true,
        .fail_enable_rx = true,
    };
    esp32_mquickjs_i2s_channel_resources_t resources = {0};
    esp32_mquickjs_i2s_channel_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_i2s_channel_resources_init(
               &resources, true, true, &ops) == 0);
    assert(esp32_mquickjs_i2s_channel_resources_start(
               &resources, &ops) == FIXTURE_ENABLE_RX_ERROR);
    assert(strcmp(fixture.log, "CTRt") == 0);
    assert(!resources.rx_enabled);
    assert(!resources.tx_enabled);
    fixture.fail_enable_rx = false;
    assert(esp32_mquickjs_i2s_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "CTRtxy") == 0);
}

static void test_single_direction_channels_are_supported(void)
{
    fixture_t rx_fixture = {.need_rx = true};
    fixture_t tx_fixture = {.need_tx = true};
    esp32_mquickjs_i2s_channel_resources_t resources = {0};
    esp32_mquickjs_i2s_channel_resource_ops_t ops = fixture_ops(&rx_fixture);

    assert(esp32_mquickjs_i2s_channel_resources_init(
               &resources, true, false, &ops) == 0);
    assert(esp32_mquickjs_i2s_channel_resources_start(
               &resources, &ops) == 0);
    assert(esp32_mquickjs_i2s_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(rx_fixture.log, "CRrx") == 0);

    ops = fixture_ops(&tx_fixture);
    assert(esp32_mquickjs_i2s_channel_resources_init(
               &resources, false, true, &ops) == 0);
    assert(esp32_mquickjs_i2s_channel_resources_start(
               &resources, &ops) == 0);
    assert(esp32_mquickjs_i2s_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(tx_fixture.log, "CTty") == 0);

    assert(esp32_mquickjs_i2s_channel_resources_init(
               &resources, false, false, &ops) != 0);
}

static void test_failed_start_rollback_retains_enabled_tx_for_retry(void)
{
    fixture_t fixture = {
        .need_tx = true,
        .need_rx = true,
        .fail_enable_rx = true,
        .fail_disable_tx = true,
    };
    esp32_mquickjs_i2s_channel_resources_t resources = {0};
    esp32_mquickjs_i2s_channel_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_i2s_channel_resources_init(
               &resources, true, true, &ops) == 0);
    assert(esp32_mquickjs_i2s_channel_resources_start(
               &resources, &ops) == FIXTURE_DISABLE_TX_ERROR);
    assert(strcmp(fixture.log, "CTRt") == 0);
    assert(!resources.rx_enabled);
    assert(resources.tx_enabled);

    fixture.fail_disable_tx = false;
    assert(esp32_mquickjs_i2s_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "CTRttxy") == 0);
}

static void test_stop_failures_retain_exact_enabled_suffix(void)
{
    fixture_t rx_fixture = {
        .need_tx = true,
        .need_rx = true,
        .fail_disable_rx = true,
    };
    fixture_t tx_fixture = {
        .need_tx = true,
        .need_rx = true,
        .fail_disable_tx = true,
    };
    esp32_mquickjs_i2s_channel_resources_t resources = {0};
    esp32_mquickjs_i2s_channel_resource_ops_t ops = fixture_ops(&rx_fixture);

    assert(esp32_mquickjs_i2s_channel_resources_init(
               &resources, true, true, &ops) == 0);
    assert(esp32_mquickjs_i2s_channel_resources_start(
               &resources, &ops) == 0);
    assert(esp32_mquickjs_i2s_channel_resources_stop(
               &resources, &ops) == FIXTURE_DISABLE_RX_ERROR);
    assert(resources.rx_enabled);
    assert(resources.tx_enabled);
    rx_fixture.fail_disable_rx = false;
    assert(esp32_mquickjs_i2s_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(rx_fixture.log, "CTRrrtxy") == 0);

    ops = fixture_ops(&tx_fixture);
    assert(esp32_mquickjs_i2s_channel_resources_init(
               &resources, true, true, &ops) == 0);
    assert(esp32_mquickjs_i2s_channel_resources_start(
               &resources, &ops) == 0);
    assert(esp32_mquickjs_i2s_channel_resources_stop(
               &resources, &ops) == FIXTURE_DISABLE_TX_ERROR);
    assert(!resources.rx_enabled);
    assert(resources.tx_enabled);
    tx_fixture.fail_disable_tx = false;
    assert(esp32_mquickjs_i2s_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(tx_fixture.log, "CTRrttxy") == 0);
}

static void test_delete_failures_retain_exact_handle_suffix(void)
{
    fixture_t rx_fixture = {
        .need_tx = true,
        .need_rx = true,
        .fail_delete_rx = true,
    };
    fixture_t tx_fixture = {
        .need_tx = true,
        .need_rx = true,
        .fail_delete_tx = true,
    };
    esp32_mquickjs_i2s_channel_resources_t resources = {0};
    esp32_mquickjs_i2s_channel_resource_ops_t ops = fixture_ops(&rx_fixture);

    assert(esp32_mquickjs_i2s_channel_resources_init(
               &resources, true, true, &ops) == 0);
    assert(esp32_mquickjs_i2s_channel_resources_deinit(
               &resources, &ops) == FIXTURE_DELETE_RX_ERROR);
    assert(resources.rx_channel != NULL);
    assert(resources.tx_channel != NULL);
    rx_fixture.fail_delete_rx = false;
    assert(esp32_mquickjs_i2s_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(rx_fixture.log, "Cxxy") == 0);

    ops = fixture_ops(&tx_fixture);
    assert(esp32_mquickjs_i2s_channel_resources_init(
               &resources, true, true, &ops) == 0);
    assert(esp32_mquickjs_i2s_channel_resources_deinit(
               &resources, &ops) == FIXTURE_DELETE_TX_ERROR);
    assert(resources.rx_channel == NULL);
    assert(resources.tx_channel != NULL);
    tx_fixture.fail_delete_tx = false;
    assert(esp32_mquickjs_i2s_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(tx_fixture.log, "Cxyy") == 0);
}

static void test_failed_init_prefers_cleanup_error_and_retains_resources(void)
{
    fixture_t fixture = {
        .need_tx = true,
        .need_rx = true,
        .fail_create = true,
        .fail_delete_rx = true,
    };
    esp32_mquickjs_i2s_channel_resources_t resources = {0};
    esp32_mquickjs_i2s_channel_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_i2s_channel_resources_init(
               &resources, true, true, &ops) == FIXTURE_DELETE_RX_ERROR);
    assert(strcmp(fixture.log, "Cx") == 0);
    assert(resources.rx_channel != NULL);
    assert(resources.tx_channel != NULL);
    fixture.fail_delete_rx = false;
    assert(esp32_mquickjs_i2s_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "Cxxy") == 0);
}

int main(void)
{
    test_creation_failure_rolls_back_partial_handles();
    test_missing_required_handle_is_rejected();
    test_duplex_start_stop_delete_are_ordered_and_idempotent();
    test_rx_start_failure_rolls_back_new_tx_enable();
    test_single_direction_channels_are_supported();
    test_failed_start_rollback_retains_enabled_tx_for_retry();
    test_stop_failures_retain_exact_enabled_suffix();
    test_delete_failures_retain_exact_handle_suffix();
    test_failed_init_prefers_cleanup_error_and_retains_resources();
    return 0;
}
