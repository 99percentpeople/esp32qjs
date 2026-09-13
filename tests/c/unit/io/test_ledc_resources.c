#include "esp32_mquickjs_ledc_resources.h"

#include <assert.h>
#include <stddef.h>

enum {
    TEST_OK = 0,
    TEST_ERR_STOP = 91,
    TEST_ERR_DECONFIGURE = 92,
    TEST_ERR_PAUSE = 93,
};

typedef struct {
    int stop_result;
    int channel_deconfigure_result;
    int pause_result;
    int timer_deconfigure_result;
    int stop_calls;
    int channel_deconfigure_calls;
    int pause_calls;
    int timer_deconfigure_calls;
} fake_ledc_t;

static int fake_stop_channel(int index, void *opaque)
{
    fake_ledc_t *fake = opaque;

    assert(index == 2);
    fake->stop_calls++;
    return fake->stop_result;
}

static int fake_deconfigure_channel(int index, void *opaque)
{
    fake_ledc_t *fake = opaque;

    assert(index == 2);
    fake->channel_deconfigure_calls++;
    return fake->channel_deconfigure_result;
}

static int fake_pause_timer(int index, void *opaque)
{
    fake_ledc_t *fake = opaque;

    assert(index == 1);
    fake->pause_calls++;
    return fake->pause_result;
}

static int fake_deconfigure_timer(int index, void *opaque)
{
    fake_ledc_t *fake = opaque;

    assert(index == 1);
    fake->timer_deconfigure_calls++;
    return fake->timer_deconfigure_result;
}

static esp32_mquickjs_ledc_resource_ops_t fake_ops(fake_ledc_t *fake)
{
    return (esp32_mquickjs_ledc_resource_ops_t){
        .stop_channel = fake_stop_channel,
        .deconfigure_channel = fake_deconfigure_channel,
        .pause_timer = fake_pause_timer,
        .deconfigure_timer = fake_deconfigure_timer,
        .opaque = fake,
    };
}

static void test_invalid_arguments_are_rejected(void)
{
    fake_ledc_t fake = {0};
    esp32_mquickjs_ledc_resource_ops_t ops = fake_ops(&fake);
    esp32_mquickjs_ledc_channel_resources_t channel = {
        .configured = true,
    };

    assert(esp32_mquickjs_ledc_channel_resources_deinit(NULL, 2, &ops) !=
           TEST_OK);
    assert(esp32_mquickjs_ledc_channel_resources_deinit(&channel, 2, NULL) !=
           TEST_OK);
    ops.stop_channel = NULL;
    assert(esp32_mquickjs_ledc_channel_resources_deinit(&channel, 2, &ops) !=
           TEST_OK);
    assert(channel.configured);
    assert(fake.stop_calls == 0);
}

static void test_channel_failure_retains_exact_suffix_for_retry(void)
{
    fake_ledc_t fake = {
        .stop_result = TEST_ERR_STOP,
    };
    esp32_mquickjs_ledc_resource_ops_t ops = fake_ops(&fake);
    esp32_mquickjs_ledc_channel_resources_t channel = {
        .configured = true,
    };

    assert(esp32_mquickjs_ledc_channel_resources_deinit(&channel, 2, &ops) ==
           TEST_ERR_STOP);
    assert(channel.configured);
    assert(!channel.stopped);
    assert(fake.stop_calls == 1);
    assert(fake.channel_deconfigure_calls == 0);

    fake.stop_result = TEST_OK;
    fake.channel_deconfigure_result = TEST_ERR_DECONFIGURE;
    assert(esp32_mquickjs_ledc_channel_resources_deinit(&channel, 2, &ops) ==
           TEST_ERR_DECONFIGURE);
    assert(channel.configured);
    assert(channel.stopped);
    assert(fake.stop_calls == 2);
    assert(fake.channel_deconfigure_calls == 1);

    fake.channel_deconfigure_result = TEST_OK;
    assert(esp32_mquickjs_ledc_channel_resources_deinit(&channel, 2, &ops) ==
           TEST_OK);
    assert(!channel.configured);
    assert(!channel.stopped);
    assert(fake.stop_calls == 2);
    assert(fake.channel_deconfigure_calls == 2);
}

static void test_timer_failure_retains_exact_suffix_for_retry(void)
{
    fake_ledc_t fake = {
        .pause_result = TEST_ERR_PAUSE,
    };
    esp32_mquickjs_ledc_resource_ops_t ops = fake_ops(&fake);
    esp32_mquickjs_ledc_timer_resources_t timer = {
        .configured = true,
    };

    assert(esp32_mquickjs_ledc_timer_resources_deinit(&timer, 1, &ops) ==
           TEST_ERR_PAUSE);
    assert(timer.configured);
    assert(!timer.paused);
    assert(fake.pause_calls == 1);
    assert(fake.timer_deconfigure_calls == 0);

    fake.pause_result = TEST_OK;
    fake.timer_deconfigure_result = TEST_ERR_DECONFIGURE;
    assert(esp32_mquickjs_ledc_timer_resources_deinit(&timer, 1, &ops) ==
           TEST_ERR_DECONFIGURE);
    assert(timer.configured);
    assert(timer.paused);
    assert(fake.pause_calls == 2);
    assert(fake.timer_deconfigure_calls == 1);

    fake.timer_deconfigure_result = TEST_OK;
    assert(esp32_mquickjs_ledc_timer_resources_deinit(&timer, 1, &ops) ==
           TEST_OK);
    assert(!timer.configured);
    assert(!timer.paused);
    assert(fake.pause_calls == 2);
    assert(fake.timer_deconfigure_calls == 2);
}

static void test_empty_resources_are_idempotent(void)
{
    fake_ledc_t fake = {0};
    esp32_mquickjs_ledc_resource_ops_t ops = fake_ops(&fake);
    esp32_mquickjs_ledc_channel_resources_t channel = {0};
    esp32_mquickjs_ledc_timer_resources_t timer = {0};

    assert(esp32_mquickjs_ledc_channel_resources_deinit(&channel, 2, &ops) ==
           TEST_OK);
    assert(esp32_mquickjs_ledc_timer_resources_deinit(&timer, 1, &ops) ==
           TEST_OK);
    assert(fake.stop_calls == 0);
    assert(fake.pause_calls == 0);
}

int main(void)
{
    test_invalid_arguments_are_rejected();
    test_channel_failure_retains_exact_suffix_for_retry();
    test_timer_failure_retains_exact_suffix_for_retry();
    test_empty_resources_are_idempotent();
    return 0;
}
