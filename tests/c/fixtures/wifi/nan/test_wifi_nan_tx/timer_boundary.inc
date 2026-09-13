
typedef struct {uint32_t timer_expire,padding[3];void *timer_arg;} ETSTimer;
typedef void *esp_timer_handle_t;
typedef struct {void(*callback)(void*);void*arg;int dispatch_method;const char *name;} esp_timer_create_args_t;
#define ESP_TIMER_TASK 0
static struct {esp_timer_create_args_t args;bool live,active;} timers[16];
static unsigned timer_creates,timer_stops,timer_deletes,timer_posts,timer_native_calls;
static int timer_create_error,timer_stop_error,timer_delete_error,timer_post_error;
static void *timer_queued_argument,*timer_native_argument;
static uint8_t timer_queued_phase,timer_native_phase;
static void (*timer_post_hook)(void);
static void *timer_node,*timer_ndl;
_Alignas(8) uint8_t s_dp[1324];
void esp32qjs_nan_setup_timer_callback(void *p){(void)p;}
void esp32qjs_nan_inactivity_timer_callback(void *p){(void)p;}
void *nan_dp_get_ndl_by_bss(void *node){assert(!locked);return node==timer_node?timer_ndl:NULL;}
static esp_err_t esp_timer_create(const esp_timer_create_args_t *a,esp_timer_handle_t *h){
 assert(!locked&&a->dispatch_method==ESP_TIMER_TASK&&timer_creates<16);
 if(timer_create_error)return timer_create_error;
 unsigned i=timer_creates++;timers[i].args=*a;timers[i].live=true;*h=&timers[i];return 0;
}
static esp_err_t esp_timer_stop(esp_timer_handle_t h){
 assert(!locked&&h);++timer_stops;if(timer_stop_error)return timer_stop_error;
 for(unsigned i=0;i<timer_creates;i++)if(h==&timers[i]){assert(timers[i].live);timers[i].active=false;return 0;}
 assert(false);return ESP_FAIL;
}
static esp_err_t esp_timer_delete(esp_timer_handle_t h){
 assert(!locked&&h);++timer_deletes;if(timer_delete_error)return timer_delete_error;
 for(unsigned i=0;i<timer_creates;i++)if(h==&timers[i]){assert(timers[i].live&&!timers[i].active);timers[i].live=false;return 0;}
 assert(false);return ESP_FAIL;
}
static esp_err_t esp_timer_start_once(esp_timer_handle_t h,uint64_t us){
 assert(!locked&&h&&us);for(unsigned i=0;i<timer_creates;i++)if(h==&timers[i]){assert(timers[i].live);timers[i].active=true;return 0;}
 assert(false);return ESP_FAIL;
}
int ieee80211_timer_process(int signal,int phase,void *argument){
 assert(!locked&&signal==7&&(phase==42||phase==43)&&argument);++timer_posts;
 timer_queued_argument=argument;timer_queued_phase=phase;if(timer_post_hook)timer_post_hook();return timer_post_error;
}
void esp32_mquickjs_wifi_nan_sdk_timer_process(uint8_t phase,void *argument){
 assert(!locked&&argument);++timer_native_calls;timer_native_argument=argument;timer_native_phase=phase;
}
