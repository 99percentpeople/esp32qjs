
#define NAN_CAPS_NDPE_ATTR 1U
#define ESP_NAN_PUBLISH 1
#define NAN_DW_INTVL_MS 512U
#define pdFALSE 0
#define pdMS_TO_TICKS(v) (v)
typedef struct {uint8_t pub_id,peer_mac[6];bool confirm_required;} wifi_nan_datapath_req_t;
struct peer_svc_info {uint8_t svc_id,type,own_svc_id,peer_nmi[6];uint32_t device_caps;};
static struct peer_svc_info peer_service={.svc_id=3,.type=ESP_NAN_PUBLISH,.own_svc_id=1,.peer_nmi={2,3,4,5,6,7}};
static bool submitted;
static struct peer_svc_info*nan_find_peer_svc(uint8_t own,uint8_t id,const uint8_t*mac){assert(depth==1&&!own&&id==3&&MACADDR_EQUAL(mac,peer));return &peer_service;}
static esp_err_t esp_wifi_get_mac(int iface,uint8_t*mac){assert(iface==WIFI_IF_NAN);memcpy(mac,peer,6);return ESP_OK;}
static bool ndl_limit_reached(void){return false;}
static void os_event_group_clear_bits(void*g,unsigned bits){assert(g==nan_event_group);event_bits&=~bits;}
static esp_err_t esp_nan_internal_datapath_req(wifi_nan_datapath_req_t*req,uint8_t*id,uint8_t*ipv6){assert(!depth&&req&&ipv6);*id=7;submitted=true;return ESP_OK;}
static void nan_ndl_finalize_ndp_id(uint8_t id,uint8_t pub,uint8_t*mac,uint32_t caps){assert(depth==1&&id==7&&pub==3&&MACADDR_EQUAL(mac,peer)&&!caps);live_ndl=true;}
static EventBits_t os_event_group_wait_bits(void*g,unsigned bits,int clear,int all,unsigned wait){assert(!depth&&g&&bits==(NDP_ACCEPTED|NDP_REJECTED)&&!clear&&!all&&wait);return 0;}
static void nan_ndl_release(uint8_t id){nan_reset_ndl(id,false);}
