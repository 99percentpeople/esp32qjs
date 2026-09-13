
static struct {esp32_mquickjs_wifi_dpp_result_status_t status;bool ever_bound;}result={.ever_bound=true};
static __typeof__(result)*s_dpp_result=&result;
static struct {bool dpp_listen_ongoing;}s_dpp_ctx;
static uint32_t s_dpp_generation=1;
static atomic_bool s_dpp_init_done=true,dpp_shutting_down,roc_in_progress;
static bool s_dpp_roc_submitted,s_dpp_roc_cancelled;
static void *s_action_rx_cb=(void*)0x1234,*s_dpp_event_group=(void*)1;
static bool native_task=true,present,synchronous,complete_cancel=true;
static esp_err_t submit_error,cancel_error,abort_error;
static unsigned submits,cancels,next_channels;
static uint8_t driver_id=7;
static unsigned bits;
static wifi_action_roc_done_cb_t native_done;
static bool current_task_is_wifi_task(void){assert(!critical);return native_task;}
static int dpp_api_lock(void){assert(!critical);depth++;return 0;}
static int dpp_api_unlock(void){assert(!critical&&depth);depth--;return 0;}
static void esp32qjs_dpp_result_count(uint32_t*p){if(*p!=UINT32_MAX)++*p;}
static void os_event_group_set_bits(void*p,unsigned value){assert(p&&!critical&&depth);bits|=value;}
static void os_event_group_clear_bits(void*p,unsigned value){assert(p&&!critical&&depth);bits&=~value;}
static int esp32_mquickjs_wifi_action_sdk_quiescent(void){assert(native_task&&!critical&&depth);return present?ESP_ERR_TIMEOUT:ESP_OK;}
static int esp32qjs_wifi_chm_timer_service_native(void){assert(native_task&&!critical&&depth);return 0;}
struct queued {eloop_timeout_handler fn;void *high,*low;};
static struct queued queue[4];
static bool queue_full;
static int esp32qjs_dpp_async_register(unsigned s,unsigned us,eloop_timeout_handler fn,void*h,void*l){
 assert(!critical&&depth&&!s&&us==10000);if(queue_full)return -1;
 for(unsigned i=0;i<4;i++)if(!queue[i].fn){queue[i]=(struct queued){fn,h,l};return 0;}return -1;
}
static int esp32qjs_dpp_async_cancel(eloop_timeout_handler fn,void*h,void*l){
 assert(!critical&&depth);int count=0;
 for(unsigned i=0;i<4;i++)if(queue[i].fn==fn&&queue[i].high==h&&queue[i].low==l){queue[i].fn=NULL;count++;}return count;
}
static struct queued detach(void){for(unsigned i=0;i<4;i++)if(queue[i].fn){struct queued q=queue[i];queue[i].fn=NULL;return q;}assert(0);return(struct queued){0};}
static int esp_wifi_remain_on_channel(wifi_roc_req_t*request);
static void dpp_listen_next_channel(void*a,void*b){(void)a;(void)b;assert(!critical);next_channels++;}
