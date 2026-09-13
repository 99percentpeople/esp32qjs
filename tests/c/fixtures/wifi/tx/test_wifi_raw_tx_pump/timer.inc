typedef struct {void (*callback)(void *);void *arg;bool active;} pump_test_timer_t;
typedef pump_test_timer_t *esp_timer_handle_t;
typedef struct {void (*callback)(void *);void *arg;const char *name;} esp_timer_create_args_t;
static pump_test_timer_t pump_test_timer;
static bool timer_fail;
static unsigned timer_creates;
static esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    ++timer_creates;if(timer_fail)return ESP_ERR_NO_MEM;
    pump_test_timer=(pump_test_timer_t){.callback=args->callback,.arg=args->arg};*out=&pump_test_timer;return ESP_OK;
}
static esp_err_t esp_timer_start_once(esp_timer_handle_t timer,uint64_t us) {
    assert(timer==&pump_test_timer && us==1000);
    if(timer->active)return ESP_ERR_INVALID_STATE;
    timer->active=true;return ESP_OK;
}
static void fire_pump(void) {
    assert(pump_test_timer.active);fake_now+=1000;pump_test_timer.active=false;
    pump_test_timer.callback(pump_test_timer.arg);
}
static void drain_pump(void) {unsigned n=0;while(job_count){assert(++n<50);run_job();}}
