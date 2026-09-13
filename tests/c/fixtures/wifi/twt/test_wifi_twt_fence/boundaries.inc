
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
