
typedef int esp_err_t;
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_NAN_PUBLISH 1
#define ESP_NAN_SUBSCRIBE 2
static int s_nan_data_lock=1,depth;
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
typedef struct {uint8_t inst_id,peer_inst_id,peer_mac[6];unsigned ssi_len;void*ssi,*vendor_ie;} wifi_nan_followup_params_t;
typedef struct {int type,status,method,reason_code;unsigned comeback_after,cookie;} wifi_nan_pairing_npba_params_t;
typedef struct {wifi_nan_pairing_npba_params_t*pairing_npba;} extra_params_internal_t;
struct own_svc_info {struct{bool pairing_setup;}pairing;int type;};
static struct own_svc_info own={.pairing.pairing_setup=true,.type=ESP_NAN_SUBSCRIBE};
static bool peer_exists;static unsigned submits;static int native_error,last_type,last_method;
static struct own_svc_info*nan_find_own_svc(uint8_t id){assert(depth==1&&id==7);return &own;}
static bool nan_find_peer_svc_exact(uint8_t own_id,uint8_t peer_id,const uint8_t*peer){
 assert(depth==1&&own_id==7&&peer_id==9&&peer[0]==2);return peer_exists;
}
static int esp_nan_internal_send_followup(wifi_nan_followup_params_t*p,uint32_t*context,extra_params_internal_t*extra){
 assert(!depth&&p&&extra&&extra->pairing_npba);++submits;
 wifi_nan_pairing_npba_params_t*n=extra->pairing_npba;last_type=n->type;last_status=n->status;last_method=n->method;
 assert(!n->reason_code&&!n->comeback_after&&!n->cookie);if(!native_error)*context=1234;return native_error;
}
