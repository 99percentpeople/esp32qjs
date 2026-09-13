
#define ESP_NAN_PUBLISH 2
#define ESP_NAN_SUBSCRIBE 1
#define NAN_SVC_ID_PENDING 255
typedef struct {char service_name[256];bool datapath_reqd;}wifi_nan_publish_cfg_t;
typedef wifi_nan_publish_cfg_t wifi_nan_subscribe_cfg_t;
struct own_svc_info{uint8_t svc_id,type;};
static struct own_svc_info own;
static unsigned depth,notifications;static bool missing_host;
static int create_result;
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
static struct own_svc_info*nan_find_own_svc_by_name(const char*name){assert(depth==1&&name);return missing_host?NULL:&own;}
int __real_nan_start_publish_service(const wifi_nan_publish_cfg_t*c,uint8_t*id){assert(!depth&&c&&own.svc_id==255);if(!create_result)*id=7;return create_result;}
int __real_nan_start_subscribe_service(const wifi_nan_subscribe_cfg_t*c,uint8_t*id){return __real_nan_start_publish_service(c,id);}
