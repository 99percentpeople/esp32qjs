
static uint8_t storage[128];
uint8_t *g_chm=storage;
static struct {esp_timer_create_args_t args;bool active,deleted;} handles[16];
static uint64_t now;
static unsigned creates,starts,stops,deletes,native_ends,native_starts;
static unsigned last_phase;
static int create_error,start_error,stop_error,delete_error,post_error;
static bool replace_during_post,reenter_on_end;
static uint32_t messages[16];
static unsigned messages_count;
static void rearm_max(uint64_t delay);
int64_t esp_timer_get_time(void){assert(!critical);return (int64_t)now;}
static esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out){
 assert(!critical&&args->dispatch_method==ESP_TIMER_TASK);if(create_error)return create_error;
 assert(++creates<16);handles[creates].args=*args;*out=creates;return 0;
}
static esp_err_t esp_timer_stop(esp_timer_handle_t handle){
 assert(!critical&&handle&&handle<=creates&&!handles[handle].deleted);stops++;
 if(stop_error)return stop_error;if(!handles[handle].active)return ESP_ERR_INVALID_STATE;
 handles[handle].active=false;return 0;
}
static esp_err_t esp_timer_start_once(esp_timer_handle_t handle,uint64_t delay){
 assert(!critical&&handle&&!handles[handle].deleted&&!handles[handle].active);(void)delay;starts++;
 if(start_error)return start_error;handles[handle].active=true;return 0;
}
static esp_err_t esp_timer_delete(esp_timer_handle_t handle){
 assert(!critical&&handle&&!handles[handle].deleted);deletes++;
 if(delete_error)return delete_error;assert(!handles[handle].active);handles[handle].deleted=true;return 0;
}
int ieee80211_timer_process(int signal,int operation,void *argument){
 assert(!critical&&signal==7&&operation==8);
 if(replace_during_post){replace_during_post=false;rearm_max(100);}
 if(post_error)return post_error;
 assert(messages_count<16);messages[messages_count++]=(uint32_t)(uintptr_t)argument;return 0;
}
int __real_chm_start_op(void*channel,uint32_t min,uint32_t max,
 void(*start)(void*,int),void(*end)(void*,int),void*context){
 (void)channel;(void)min;(void)max;(void)start;(void)end;(void)context;
 assert(!critical);native_starts++;return 0;
}
static void original_callback(void *argument){(void)argument;assert(0);}
