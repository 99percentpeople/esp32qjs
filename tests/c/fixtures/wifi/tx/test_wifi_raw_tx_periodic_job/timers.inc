
#define ESP_TIMER_TASK 0
#define ESP32_MQUICKJS_MEMORY_EXTERNAL 1
static bool in_timer;
static unsigned timer_creates,timer_stops,timer_deletes;
static esp_err_t create_error,start_error,timer_stop_error,delete_error;
typedef struct {void (*callback)(void *);void *arg;bool allocated,active;} fake_timer_t;
typedef fake_timer_t *esp_timer_handle_t;
typedef struct {void (*callback)(void *);void *arg;int dispatch_method;const char *name;bool skip_unhandled_events;} esp_timer_create_args_t;
static fake_timer_t timers[8];
static esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *output) {
    assert(!in_timer && !critical_depth && !session_mutex_depth && !*output);++timer_creates;
    assert(args->dispatch_method==ESP_TIMER_TASK && args->skip_unhandled_events);
    if(create_error)return create_error;
    for(unsigned i=0;i<8;++i)if(!timers[i].allocated){timers[i]=(fake_timer_t){args->callback,args->arg,true,false};*output=&timers[i];return ESP_OK;}
    return ESP_ERR_NO_MEM;
}
static esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer,uint64_t us) {
    assert(!in_timer && !critical_depth && !session_mutex_depth && timer->allocated && us==1000);
    if(start_error) return start_error;
    timer->active=true;return ESP_OK;
}
static esp_err_t esp_timer_stop_blocking(esp_timer_handle_t timer,uint32_t wait) {
    assert(!in_timer && !critical_depth && !session_mutex_depth && timer->allocated && wait==1);
    ++timer_stops;timer->active=false;return timer_stop_error;
}
static esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    assert(!in_timer && !critical_depth && !session_mutex_depth && timer->allocated && !timer->active);
    ++timer_deletes;if(delete_error)return delete_error;
    timer->allocated=false;return ESP_OK;
}
static void fire_timer(esp_timer_handle_t timer) {
    assert(timer->allocated && timer->active && !in_timer && !job_count);
    fake_now+=1000;unsigned before_alloc=allocations,before_free=frees;
    in_timer=true;timer->callback(timer->arg);in_timer=false;
    assert(allocations==before_alloc && frees==before_free && job_count<=1);
}
static void drain_workers(void) {unsigned n=0;while(job_count){assert(++n<40);run_job();}}
