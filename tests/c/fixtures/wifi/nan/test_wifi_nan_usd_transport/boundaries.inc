
struct nan_de{int unused;};static struct nan_de engine,*g_nan_de=&engine;
static void*s_nan_usd_data_lock=&engine;
static uint32_t s_esp32qjs_usd_identity=1;
static bool s_esp32qjs_usd_closing,busy,early,timer_failure;
static int s_esp32qjs_usd_error;
static uint64_t ticket;
static unsigned allocations,frees,submits,cancels;
static uint8_t last_byte;
static esp32qjs_wifi_offchan_tx_status_t physical;
#define NAN_USD_DATA_LOCK() ((void)0)
#define NAN_USD_DATA_UNLOCK() ((void)0)
bool current_task_is_wifi_task(void){return true;}
int esp_nan_de_rx_action(uint8_t*h,uint8_t*p,size_t n,uint8_t c){(void)h;(void)p;(void)n;(void)c;return 0;}
static void*esp32_mquickjs_memory_wireless_calloc(const char*tag,size_t n,size_t size,int cls,int budget){
 assert(!strcmp(tag,"wifi.nan")&&!cls&&budget==1);void*p=calloc(n,size);if(p)allocations++;return p;}
static void esp32_mquickjs_memory_payload_free(void*p){if(p){frees++;free(p);}}
static void forced_memzero(void*p,size_t n){memset(p,0,n);}
static void esp32qjs_nan_usd_record_error(int error){if(!s_esp32qjs_usd_error)s_esp32qjs_usd_error=error;}
static int64_t esp_timer_get_time(void){return 1234;}
static int esp_nan_chan_to_freq(int channel){return 2407+channel*5;}
static int eloop_register_timeout(unsigned s,unsigned us,void(*fn)(void*,void*),void*a,void*b){
 (void)s;(void)us;(void)fn;(void)a;(void)b;return timer_failure?-1:0;}
static void eloop_cancel_timeout(void(*fn)(void*,void*),void*a,void*b){(void)fn;(void)a;(void)b;}
static int eloop_register_timeout_blocking(int(*fn)(void*,void*),void*a,void*b){return fn(a,b);}
static int esp32qjs_wifi_chm_timer_service_native(void){return 0;}
static int esp32_mquickjs_wifi_action_sdk_quiescent(void){return busy?ESP_ERR_TIMEOUT:0;}
esp_err_t esp32qjs_wifi_offchan_tx_allocate_ticket(uint64_t*out){assert(!*out);*out=++ticket;return 0;}
esp_err_t esp32qjs_wifi_offchan_tx_reserve(uint64_t id,uint32_t context,uint8_t channel){
 assert(!physical.held&&context==(uint32_t)(uintptr_t)esp_nan_de_rx_action&&channel==6);
 physical=(esp32qjs_wifi_offchan_tx_status_t){.ticket=id,.held=true};return 0;}
esp_err_t esp32qjs_wifi_offchan_tx_status(uint64_t id,esp32qjs_wifi_offchan_tx_status_t*out){
 if(!physical.held||id!=physical.ticket)return ESP_ERR_INVALID_STATE;
 *out=physical;return 0;}
esp_err_t esp32qjs_wifi_offchan_tx_poll_native(uint64_t id){assert(physical.held&&id==physical.ticket);return 0;}
esp_err_t esp32qjs_wifi_offchan_tx_release(uint64_t id){
 assert(physical.held&&id==physical.ticket&&!physical.buffer_present&&!physical.recycling);physical.held=false;return 0;}
static int esp_wifi_action_tx_req(wifi_action_tx_req_t*request){
 if(request->type==WIFI_OFFCHAN_TX_CANCEL){cancels++;busy=false;return 0;}
 assert(!busy&&physical.held);submits++;last_byte=request->data[0];busy=physical.buffer_present=true;request->op_id=7;
 if(early){wifi_event_action_tx_status_t e={.ifx=0,.status=WIFI_ACTION_TX_DONE,
 .context=(uintptr_t)esp_nan_de_rx_action,.channel=6,.op_id=7};esp32qjs_nan_usd_tx_status_capture(&e);}return 0;}
static int esp_wifi_remain_on_channel(wifi_roc_req_t*request){(void)request;return ESP_ERR_INVALID_STATE;}
static void nan_de_listen_ended(struct nan_de*de,int frequency){assert(de==g_nan_de&&frequency);}
static void nan_de_tx_status(struct nan_de*de,int frequency,const void*peer){assert(de==g_nan_de&&frequency&&!peer);}
static void nan_de_tx_wait_ended(struct nan_de*de){assert(de==g_nan_de);}
void esp32_mquickjs_wifi_nan_usd_sdk_status(esp32_mquickjs_wifi_nan_usd_sdk_status_t*status);
