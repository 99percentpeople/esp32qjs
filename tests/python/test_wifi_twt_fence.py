"""Deferred production TWT timer/native queue ordering and storage retirement.

Only esp_timer and native ioctl outcomes/scheduling are replaced. No alternate
TWT lifecycle, no fixture import/compile/run during API implementation.
"""
import unittest
from test_wifi_action_lane import PRELUDE
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wifi_config_controls import structure
from test_wireless_control_regression import compile_run


class WiFiTwtFence(unittest.TestCase):
    def test_early_callback_cancel_late_exit_exact_revision_and_failed_suffix(self):
        lane = (COMPONENT / 'internal/esp32_mquickjs_wifi_twt_lane.h').read_text()
        code = PRELUDE + '\n#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n#include <stdatomic.h>\n'
        code += structure(lane, 'esp32_mquickjs_wifi_twt_token_t')
        code += BOUNDARIES
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_fence.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_fence.c')
        compile_run(self, code + MAIN)


BOUNDARIES = r'''
#define ESP_ERR_NOT_FINISHED 31
#define ESP_ERR_TIMEOUT 32
#define ESP_TIMER_TASK 0
typedef struct timer_native *esp_timer_handle_t;
typedef struct {void (*callback)(void *);void *arg;unsigned dispatch_method;const char *name;} esp_timer_create_args_t;
static struct timer_native {void (*callback)(void *);void *arg;bool live,armed,running;} timer_native;
static unsigned creates,starts,stops,deletes,native_calls;
static int create_error,start_error,stop_error,delete_error,native_error;
static bool early_callback,hold_callback_exit;
static void fire(void) {
    assert(timer_native.live && timer_native.armed && !timer_native.running);
    timer_native.armed=false;timer_native.running=true;
    timer_native.callback(timer_native.arg);
    /* Model the scheduling boundary between the production callback's final
     * atomic store and the SDK clearing CALLBACK_IS_RUNNING after its return. */
    if(!hold_callback_exit)timer_native.running=false;
}
static int esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    ++creates;assert(args && args->callback && args->arg && args->dispatch_method==ESP_TIMER_TASK && args->name);
    assert(out && !*out && !timer_native.live);
    if(create_error)return create_error;
    timer_native=(struct timer_native){.callback=args->callback,.arg=args->arg,.live=true};
    *out=&timer_native;return ESP_OK;
}
static int esp_timer_start_once(esp_timer_handle_t timer,uint64_t delay) {
    ++starts;assert(timer==&timer_native && timer->live && !timer->armed && delay==1);
    if(start_error)return start_error;
    timer->armed=true;if(early_callback)fire();return ESP_OK;
}
static int esp_timer_stop_blocking(esp_timer_handle_t timer,uint32_t ticks) {
    ++stops;assert(timer==&timer_native && timer->live && ticks==1);
    timer->armed=false;
    if(stop_error)return stop_error;
    return timer->running?ESP_ERR_TIMEOUT:ESP_OK;
}
static int esp_timer_delete(esp_timer_handle_t timer) {
    ++deletes;assert(timer==&timer_native && timer->live && !timer->armed && !timer->running);
    if(delete_error)return delete_error;
    timer->live=false;return ESP_OK;
}
static int esp32_mquickjs_wifi_action_sdk_fence(void) {
    ++native_calls;assert(!timer_native.live && !timer_native.running && !timer_native.armed);
    return native_error;
}
'''
MAIN = r'''
#define api(name) esp32_mquickjs_wifi_twt_fence_##name
typedef esp32_mquickjs_wifi_twt_fence_t fence_t;
typedef esp32_mquickjs_wifi_twt_token_t token_t;
static token_t token={.identity=UINT64_C(0x123456789abcdef0),.generation=17};
static void reset_driver(void) {
    assert(!timer_native.live && !timer_native.running);
    timer_native=(struct timer_native){0};
    creates=starts=stops=deletes=native_calls=0;
    create_error=start_error=stop_error=delete_error=native_error=0;
    early_callback=hold_callback_exit=false;
}
static void failed_start(void) {
    fence_t f={0};reset_driver();
    assert(api(begin)(NULL,&token,1)==ESP_ERR_INVALID_ARG);
    assert(api(begin)(&f,NULL,1)==ESP_ERR_INVALID_ARG);
    assert(api(begin)(&f,&token,UINT32_MAX)==ESP_ERR_INVALID_ARG && !creates);
    token_t bad=token;bad.generation=0;
    assert(api(begin)(&f,&bad,1)==ESP_ERR_INVALID_ARG && !creates);
    create_error=71;
    assert(api(begin)(&f,&token,1)==71 && f.start_error==71 && !f.timer);
    assert(!strcmp(f.stage,"twt-timer-create"));
    assert(api(begin)(&f,&token,1)==ESP_ERR_INVALID_STATE && creates==1);
    assert(api(poll)(&f,&token,1)==71 && !stops && !deletes && !native_calls);
    assert(api(clear)(&f,&token,1)==ESP_OK && !f.token.identity);
    reset_driver();start_error=72;
    assert(api(begin)(&f,&token,2)==72 && creates==1 && starts==1 && f.timer);
    assert(!strcmp(f.stage,"twt-timer-start"));
    stop_error=73;
    assert(api(clear)(&f,&token,2)==73 && f.start_error==72 && f.cleanup_error==73 && f.timer);
    assert(f.clearing && !strcmp(f.stage,"twt-timer-stop") && !deletes);
    assert(api(poll)(&f,&token,2)==ESP_ERR_INVALID_STATE);
    stop_error=0;delete_error=74;
    assert(api(clear)(&f,&token,2)==74 && f.stopped && stops==2 && deletes==1);
    assert(f.start_error==72 && !strcmp(f.stage,"twt-timer-delete"));
    delete_error=0;
    assert(api(clear)(&f,&token,2)==ESP_OK && stops==2 && deletes==2 && !f.timer && !native_calls);
    assert(api(clear)(&f,&token,2)==ESP_ERR_INVALID_STATE && deletes==2);
}
static void ordering_and_retry(void) {
    for(unsigned early=0;early<2;++early) {
        fence_t f={0};reset_driver();early_callback=early;hold_callback_exit=true;
        assert(api(begin)(&f,&token,3)==ESP_OK && f.started && !f.ready);
        token_t wrong=token;wrong.identity++;
        assert(api(poll)(&f,&wrong,3)==ESP_ERR_INVALID_STATE);
        assert(api(clear)(&f,&wrong,3)==ESP_ERR_INVALID_STATE);
        wrong=token;wrong.generation++;
        assert(api(poll)(&f,&wrong,3)==ESP_ERR_INVALID_STATE);
        assert(api(poll)(&f,&token,4)==ESP_ERR_INVALID_STATE && !stops && !native_calls);
        if(!early) {
            assert(api(poll)(&f,&token,3)==ESP_ERR_NOT_FINISHED && !stops && !deletes && !native_calls);
            fire();
        }
        assert(atomic_load(&f.reached) && timer_native.running);
        assert(api(poll)(&f,&token,3)==ESP_ERR_TIMEOUT && !deletes && !native_calls && f.timer);
        timer_native.running=false;delete_error=81;
        assert(api(poll)(&f,&token,3)==81 && f.stopped && stops==2 && deletes==1 && !native_calls);
        delete_error=0;native_error=82;
        assert(api(poll)(&f,&token,3)==82 && !f.timer && stops==2 && deletes==2 && native_calls==1);
        assert(!f.ready && !strcmp(f.stage,"twt-native-fence"));
        native_error=0;
        assert(api(poll)(&f,&token,3)==ESP_OK && f.ready && !f.stage && !f.cleanup_error);
        assert(stops==2 && deletes==2 && native_calls==2);
        assert(api(poll)(&f,&token,3)==ESP_OK && native_calls==2);
        assert(api(begin)(&f,&token,4)==ESP_ERR_INVALID_STATE && creates==1);
        assert(api(clear)(&f,&token,4)==ESP_ERR_INVALID_STATE && f.ready);
        assert(api(clear)(&f,&token,3)==ESP_OK && !f.token.identity && !atomic_load(&f.reached));
        assert(stops==2 && deletes==2 && native_calls==2);
        /* A fresh revision never consumes the previous callback's reached bit. */
        early_callback=false;hold_callback_exit=false;
        assert(api(begin)(&f,&token,4)==ESP_OK && creates==2);
        assert(api(poll)(&f,&token,4)==ESP_ERR_NOT_FINISHED);
        assert(api(poll)(&f,&token,3)==ESP_ERR_INVALID_STATE);
        assert(api(clear)(&f,&token,4)==ESP_OK && native_calls==2);
    }
}
static void close_and_late_callback(void) {
    fence_t f={0};reset_driver();
    assert(api(begin)(&f,&token,5)==ESP_OK);
    assert(api(clear)(&f,&token,5)==ESP_OK && !atomic_load(&f.reached) && !native_calls);
    assert(!timer_native.live && !timer_native.armed);
    assert(api(begin)(&f,&token,6)==ESP_OK);
    hold_callback_exit=true;fire();
    assert(api(clear)(&f,&token,6)==ESP_ERR_TIMEOUT && f.clearing && f.timer);
    assert(api(begin)(&f,&token,7)==ESP_ERR_INVALID_STATE);
    assert(api(poll)(&f,&token,6)==ESP_ERR_INVALID_STATE && !native_calls);
    timer_native.running=false;
    assert(api(clear)(&f,&token,6)==ESP_OK && !timer_native.live && !f.token.identity);
}
int main(void) {
    failed_start();ordering_and_retry();close_and_late_callback();return 0;
}
'''
