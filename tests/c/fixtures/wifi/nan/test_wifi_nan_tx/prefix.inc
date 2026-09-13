
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
#define CONFIG_IDF_TARGET_ESP32C5 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 11
#define ESP_ERR_NO_MEM 12
#define ESP_ERR_TIMEOUT 13
#define ESP_ERR_INVALID_RESPONSE 14
#define ESP_ERR_INVALID_ARG 15
#define ESP_FAIL 16
#define ESP_WIFI_NAN_DATAPATH_MAX_PEERS 2
#define ESP_WIFI_MAX_SVC_SSI_LEN 512
typedef struct {uint8_t pub_id,peer_mac[6];bool confirm_required;} wifi_nan_datapath_req_t;
#define ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL 1
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 2
#define IRAM_ATTR
typedef int esp_err_t,portMUX_TYPE;
typedef void *TaskHandle_t;
static TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void *)1;}
static int64_t esp_timer_get_time(void){return 100;}
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned locked,heap_live,native_calls;
static bool fail_heap,fail_native;
static unsigned next_buffer;
static uint8_t buffers[64][80],metadata[64][32];
#define portENTER_CRITICAL_SAFE(p) do{(void)(p);assert(!locked);locked=1;}while(0)
#define portEXIT_CRITICAL_SAFE(p) do{(void)(p);assert(locked);locked=0;}while(0)
static void *esp32_mquickjs_memory_wireless_calloc(const char *owner,size_t count,size_t size,int policy,int role) {
    assert(!locked&&!strcmp(owner,"wifi.nan")&&policy==1&&role==2);
    if(fail_heap)return NULL;
    void *p=calloc(count,size);assert(p);++heap_live;return p;
}
static void esp32_mquickjs_memory_payload_free(void *p){assert(!locked&&p&&heap_live);--heap_live;free(p);}
void *nan_alloc_sdf(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e) {
    assert(!locked&&a==1&&b==2&&c==3&&d==4&&e==5);++native_calls;
    if(fail_native)return NULL;
    unsigned i=next_buffer++%64;
    memset(buffers[i],0,sizeof(buffers[i]));memset(metadata[i],0,sizeof(metadata[i]));
    uint8_t *m=metadata[i];memcpy(buffers[i]+56,&m,sizeof(m));return buffers[i];
}
void *nan_alloc_action(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e) {
    assert(b&&((const uint8_t*)b)[0]==2);return nan_alloc_sdf(a,2,c,d,e);
}
