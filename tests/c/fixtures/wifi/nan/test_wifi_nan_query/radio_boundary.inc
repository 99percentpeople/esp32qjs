
typedef struct{uint32_t identity,generation,lease_identity;int kind;}esp32_mquickjs_wifi_radio_operation_t;
typedef struct{uint32_t identity;bool acquired;}lease_t;
#define ESP32_MQUICKJS_WIFI_RADIO_OPERATION_NAN 3
static struct{esp32_mquickjs_wifi_radio_operation_t operation;uint32_t generation;}s_radio;
static struct{lease_t lease;struct{bool ready,closing,usd;}status;}binding,*s_nan_radio=&binding;
static bool locked;
static unsigned reads;
static int native_error;
static bool wifi_radio_lease_valid(const lease_t*l){return l->acquired;}
static void wifi_radio_operation_lock(void){assert(!locked);locked=true;}
static void wifi_radio_operation_unlock(void){assert(locked);locked=false;}
esp_err_t esp32_mquickjs_wifi_nan_sdk_query(const esp32_mquickjs_wifi_nan_query_t*q,esp32_mquickjs_wifi_nan_query_result_t*out){
 assert(locked&&q&&out);++reads;if(!native_error)out->service_id=7;return native_error;
}
