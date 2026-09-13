#include "esp32_mquickjs_rmt_channel_resources.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    FIXTURE_CREATE_CHANNEL_ERROR = 501,
    FIXTURE_CREATE_ENCODER_ERROR = 502,
    FIXTURE_REGISTER_ERROR = 503,
    FIXTURE_DISABLE_ERROR = 504,
    FIXTURE_DELETE_ENCODER_ERROR = 505,
    FIXTURE_DELETE_CHANNEL_ERROR = 506,
};

typedef struct {
    bool fail_create_channel;
    bool omit_channel;
    bool fail_create_encoder;
    bool omit_encoder;
    bool fail_register;
    bool fail_disable;
    bool fail_delete_encoder;
    bool fail_delete_channel;
    char log[24];
    size_t log_length;
} fixture_t;

static int fixture_create_channel(void *opaque, void **out_channel)
{
    fixture_t *fixture = opaque;

    fixture->log[fixture->log_length++] = 'C';
    if (fixture->fail_create_channel) {
        return FIXTURE_CREATE_CHANNEL_ERROR;
    }
    if (!fixture->omit_channel) {
        *out_channel = (void *)(uintptr_t)0x11U;
    }
    return 0;
}

static int fixture_create_encoder(void *opaque, void **out_encoder)
{
    fixture_t *fixture = opaque;

    fixture->log[fixture->log_length++] = 'E';
    if (fixture->fail_create_encoder) {
        return FIXTURE_CREATE_ENCODER_ERROR;
    }
    if (!fixture->omit_encoder) {
        *out_encoder = (void *)(uintptr_t)0x22U;
    }
    return 0;
}

static int fixture_register_callbacks(void *channel, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(channel == (void *)(uintptr_t)0x11U);
    fixture->log[fixture->log_length++] = 'R';
    return fixture->fail_register ? FIXTURE_REGISTER_ERROR : 0;
}

static int fixture_disable_channel(void *channel, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(channel == (void *)(uintptr_t)0x11U);
    fixture->log[fixture->log_length++] = 'S';
    return fixture->fail_disable ? FIXTURE_DISABLE_ERROR : 0;
}

static int fixture_delete_encoder(void *encoder, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(encoder == (void *)(uintptr_t)0x22U);
    fixture->log[fixture->log_length++] = 'e';
    return fixture->fail_delete_encoder ? FIXTURE_DELETE_ENCODER_ERROR : 0;
}

static int fixture_delete_channel(void *channel, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(channel == (void *)(uintptr_t)0x11U);
    fixture->log[fixture->log_length++] = 'c';
    return fixture->fail_delete_channel ? FIXTURE_DELETE_CHANNEL_ERROR : 0;
}

static esp32_mquickjs_rmt_channel_resource_ops_t fixture_ops(
    fixture_t *fixture)
{
    return (esp32_mquickjs_rmt_channel_resource_ops_t){
        .create_channel = fixture_create_channel,
        .create_encoder = fixture_create_encoder,
        .register_callbacks = fixture_register_callbacks,
        .disable_channel = fixture_disable_channel,
        .delete_encoder = fixture_delete_encoder,
        .delete_channel = fixture_delete_channel,
        .opaque = fixture,
    };
}

static void test_channel_creation_failure_stops_immediately(void)
{
    fixture_t fixture = {.fail_create_channel = true};
    esp32_mquickjs_rmt_channel_resources_t resources = {0};
    esp32_mquickjs_rmt_channel_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_rmt_channel_resources_init(
               &resources, false, &ops) == FIXTURE_CREATE_CHANNEL_ERROR);
    assert(strcmp(fixture.log, "C") == 0);
    assert(resources.channel == NULL);
    assert(resources.encoder == NULL);
}

static void test_encoder_failure_deletes_created_channel(void)
{
    fixture_t fixture = {.fail_create_encoder = true};
    esp32_mquickjs_rmt_channel_resources_t resources = {0};
    esp32_mquickjs_rmt_channel_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_rmt_channel_resources_init(
               &resources, true, &ops) == FIXTURE_CREATE_ENCODER_ERROR);
    assert(strcmp(fixture.log, "CEc") == 0);
    assert(resources.channel == NULL);
    assert(resources.encoder == NULL);
}

static void test_missing_handles_are_rejected_and_rolled_back(void)
{
    fixture_t missing_channel = {.omit_channel = true};
    fixture_t missing_encoder = {.omit_encoder = true};
    esp32_mquickjs_rmt_channel_resources_t resources = {0};
    esp32_mquickjs_rmt_channel_resource_ops_t ops = fixture_ops(&missing_channel);

    assert(esp32_mquickjs_rmt_channel_resources_init(
               &resources, false, &ops) != 0);
    assert(strcmp(missing_channel.log, "C") == 0);

    ops = fixture_ops(&missing_encoder);
    assert(esp32_mquickjs_rmt_channel_resources_init(
               &resources, true, &ops) != 0);
    assert(strcmp(missing_encoder.log, "CEc") == 0);
    assert(resources.channel == NULL);
    assert(resources.encoder == NULL);
}

static void test_callback_failure_rolls_back_encoder_then_channel(void)
{
    fixture_t fixture = {.fail_register = true};
    esp32_mquickjs_rmt_channel_resources_t resources = {0};
    esp32_mquickjs_rmt_channel_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_rmt_channel_resources_init(
               &resources, true, &ops) == FIXTURE_REGISTER_ERROR);
    assert(strcmp(fixture.log, "CERec") == 0);
    assert(resources.channel == NULL);
    assert(resources.encoder == NULL);
}

static void test_failed_init_retains_resources_when_rollback_fails(void)
{
    fixture_t fixture = {
        .fail_register = true,
        .fail_delete_encoder = true,
    };
    esp32_mquickjs_rmt_channel_resources_t resources = {0};
    esp32_mquickjs_rmt_channel_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_rmt_channel_resources_init(
               &resources, true, &ops) == FIXTURE_DELETE_ENCODER_ERROR);
    assert(strcmp(fixture.log, "CERe") == 0);
    assert(resources.channel != NULL);
    assert(resources.encoder != NULL);

    fixture.fail_delete_encoder = false;
    assert(esp32_mquickjs_rmt_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "CEReec") == 0);
}

static void test_successful_tx_cleanup_is_reverse_and_idempotent(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_rmt_channel_resources_t resources = {0};
    esp32_mquickjs_rmt_channel_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_rmt_channel_resources_init(
               &resources, true, &ops) == 0);
    resources.enabled = true;
    assert(esp32_mquickjs_rmt_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "CERSec") == 0);
    assert(resources.channel == NULL);
    assert(resources.encoder == NULL);
    assert(!resources.enabled);
    assert(esp32_mquickjs_rmt_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "CERSec") == 0);
}

static void test_disable_failure_retains_every_resource_for_retry(void)
{
    fixture_t fixture = {.fail_disable = true};
    esp32_mquickjs_rmt_channel_resources_t resources = {0};
    esp32_mquickjs_rmt_channel_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_rmt_channel_resources_init(
               &resources, true, &ops) == 0);
    resources.enabled = true;
    assert(esp32_mquickjs_rmt_channel_resources_deinit(
               &resources, &ops) == FIXTURE_DISABLE_ERROR);
    assert(strcmp(fixture.log, "CERS") == 0);
    assert(resources.enabled);
    assert(resources.channel != NULL);
    assert(resources.encoder != NULL);

    fixture.fail_disable = false;
    assert(esp32_mquickjs_rmt_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "CERSSec") == 0);
}

static void test_encoder_delete_failure_retains_encoder_and_channel(void)
{
    fixture_t fixture = {.fail_delete_encoder = true};
    esp32_mquickjs_rmt_channel_resources_t resources = {0};
    esp32_mquickjs_rmt_channel_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_rmt_channel_resources_init(
               &resources, true, &ops) == 0);
    resources.enabled = true;
    assert(esp32_mquickjs_rmt_channel_resources_deinit(
               &resources, &ops) == FIXTURE_DELETE_ENCODER_ERROR);
    assert(strcmp(fixture.log, "CERSe") == 0);
    assert(!resources.enabled);
    assert(resources.channel != NULL);
    assert(resources.encoder != NULL);

    fixture.fail_delete_encoder = false;
    assert(esp32_mquickjs_rmt_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "CERSeec") == 0);
}

static void test_channel_delete_failure_retains_only_channel(void)
{
    fixture_t fixture = {.fail_delete_channel = true};
    esp32_mquickjs_rmt_channel_resources_t resources = {0};
    esp32_mquickjs_rmt_channel_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_rmt_channel_resources_init(
               &resources, false, &ops) == 0);
    resources.enabled = true;
    assert(esp32_mquickjs_rmt_channel_resources_deinit(
               &resources, &ops) == FIXTURE_DELETE_CHANNEL_ERROR);
    assert(strcmp(fixture.log, "CRSc") == 0);
    assert(!resources.enabled);
    assert(resources.channel != NULL);
    assert(resources.encoder == NULL);

    fixture.fail_delete_channel = false;
    assert(esp32_mquickjs_rmt_channel_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "CRScc") == 0);
}

int main(void)
{
    test_channel_creation_failure_stops_immediately();
    test_encoder_failure_deletes_created_channel();
    test_missing_handles_are_rejected_and_rolled_back();
    test_callback_failure_rolls_back_encoder_then_channel();
    test_failed_init_retains_resources_when_rollback_fails();
    test_successful_tx_cleanup_is_reverse_and_idempotent();
    test_disable_failure_retains_every_resource_for_retry();
    test_encoder_delete_failure_retains_encoder_and_channel();
    test_channel_delete_failure_retains_only_channel();
    return 0;
}
