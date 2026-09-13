
static struct {esp32_mquickjs_wifi_dpp_result_status_t status;bool ever_bound;}result={.ever_bound=true};
static __typeof__(result)*s_dpp_result=&result;
static struct {struct{uint8_t op_id;enum dpp_tx_frame_type type;}pending_tx_op;bool pending_tx_op_in_progress;}s_dpp_ctx;
static uint32_t s_dpp_generation=1;
static atomic_bool s_dpp_init_done=true,dpp_shutting_down;
static bool s_dpp_tx_submitted,s_dpp_tx_cancelled;
static void *s_action_rx_cb=(void*)0x1234;
static bool native_task=true,present,early,finish_cancel=true,roc_held,early_wrong_id;
static esp_err_t submit_error,cancel_error,abort_error;
static unsigned submits,cancels,protocol_calls;
static uint8_t driver_id=7;
static int last_protocol_status;
static bool current_task_is_wifi_task(void){assert(!critical);return native_task;}
static int dpp_api_lock(void){assert(!critical);depth++;return 0;}
static int dpp_api_unlock(void){assert(!critical&&depth);depth--;return 0;}
static void esp32qjs_dpp_result_count(uint32_t*p){if(*p!=UINT32_MAX)++*p;}
static bool esp32qjs_dpp_roc_held(void){return roc_held;}
static int esp32_mquickjs_wifi_action_sdk_quiescent(void){assert(native_task&&!critical&&depth);return present?ESP_ERR_TIMEOUT:ESP_OK;}
static int esp32qjs_wifi_chm_timer_service_native(void){assert(native_task&&!critical&&depth);return 0;}
/* Controlled physical driver boundary. The real frame/recycler helper has
 * separate production-source coverage in test_wifi_offchan_frame.py. */
static esp32qjs_wifi_offchan_tx_status_t physical;
static bool hold_frames,allocation_fails;
static unsigned allocations,frees;
static size_t allocated_size;
static uint8_t submitted_byte;
static uint64_t allocated_ticket;
esp_err_t esp32qjs_wifi_offchan_tx_allocate_ticket(uint64_t *ticket){
 if(!ticket||*ticket)return ESP_ERR_INVALID_ARG;
 if(allocated_ticket==UINT64_MAX)return ESP_ERR_NO_MEM;
 *ticket=++allocated_ticket;return ESP_OK;
}
esp_err_t esp32qjs_wifi_offchan_tx_reserve(uint64_t ticket,uint32_t context,uint8_t channel){
 assert(!physical.held&&context==0x1234&&channel==6);
 physical=(esp32qjs_wifi_offchan_tx_status_t){.held=true,.ticket=ticket};return 0;
}
esp_err_t esp32qjs_wifi_offchan_tx_status(uint64_t ticket,esp32qjs_wifi_offchan_tx_status_t*out){
 assert(!critical);if(!physical.held||physical.ticket!=ticket)return ESP_ERR_INVALID_STATE;*out=physical;return 0;
}
esp_err_t esp32qjs_wifi_offchan_tx_poll_native(uint64_t ticket){assert(!critical&&physical.held&&physical.ticket==ticket);return 0;}
esp_err_t esp32qjs_wifi_offchan_tx_release(uint64_t ticket){
 assert(!critical&&physical.held&&physical.ticket==ticket);
 if(physical.buffer_present||physical.recycling)return ESP_ERR_NOT_FINISHED;
 physical.held=false;return 0;
}
static void*os_zalloc(size_t size){assert(!critical&&depth);if(allocation_fails)return NULL;
 assert(allocations==frees);void*p=calloc(1,size);assert(p);allocated_size=size;allocations++;return p;}
#define os_memcpy memcpy
static void bin_clear_free(void*p,size_t size){assert(!critical&&depth&&p&&size==allocated_size);
 memset(p,0,size);free(p);frees++;}
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
static int esp_wifi_action_tx_req(wifi_action_tx_req_t*request);
static void dpp_abort_failure_locked(uint32_t error){assert(depth&&!critical);abort_error=error;dpp_api_unlock();}
