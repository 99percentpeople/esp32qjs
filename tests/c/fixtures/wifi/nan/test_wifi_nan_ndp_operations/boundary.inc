
#define ESP_ERR_NOT_FOUND 17
#define ESP_WIFI_NAN_MAX_SVC_SUPPORTED 2
#define ESP_WIFI_MAX_SVC_SSI_LEN 512
#define NAN_SVC_ID_PENDING 255
#define ESP_NAN_PUBLISH 1
#define ESP_NAN_SUBSCRIBE 2
#define ESP_WIFI_NDP_ROLE_INITIATOR 1
#define ESP_WIFI_NDP_ROLE_RESPONDER 2
#define NAN_CAPS_NDPE_ATTR 4
#define WIFI_IF_NAN 3
#define WIFI_EVENT 4
#define WIFI_EVENT_NDP_TERMINATED 5
#define MACADDR_EQUAL(a,b) (!memcmp(a,b,6))
#define MACADDR_COPY(a,b) memcpy(a,b,6)
typedef struct {uint32_t addr[4];} ip6_addr_t;
typedef struct {struct {ip6_addr_t ip6;}u_addr;} ip_addr_t;
typedef struct {int sentinel;} esp_netif_t;
typedef struct {bool accept;uint8_t ndp_id,peer_mac[6],*ssi;uint16_t ssi_len;} wifi_nan_datapath_resp_t;
typedef struct {uint8_t ndp_id,peer_mac[6];} wifi_nan_datapath_end_req_t;
struct own_svc_info {uint8_t prefix[4],svc_id,type;};
struct peer_svc_info {uint8_t type;uint32_t device_caps;};
struct ndl_info {uint8_t ndp_id,publisher_id,peer[6],own_role;uint32_t device_caps;};
static struct peer_svc_info peer_service={ESP_NAN_PUBLISH,0};
static struct ndl_info host;
static esp_netif_t netif;
static struct {struct own_svc_info own_svc[2];esp_netif_t *nan_netif;} s_nan_ctx={
 .own_svc={{{0},7,ESP_NAN_SUBSCRIBE},{{0},8,ESP_NAN_SUBSCRIBE}},.nan_netif=&netif};
static unsigned depth,request_calls,response_calls,end_calls,delete_calls,disconnected;
static int request_error,response_error,end_error;
static bool s_usd_in_progress,peer_found=true;
static void *s_nan_data_lock=(void*)1,*nan_event_group=(void*)2;
static const uint8_t null_mac[6];
static uint8_t native_ndl[580];
uint8_t s_ndp_id=9;
#define NAN_DATA_LOCK() do{assert(!depth&&!locked);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
static bool esp32qjs_nan_is_discovery_only(uint8_t id){return id!=7;}
static struct own_svc_info *nan_find_own_svc(uint8_t id){assert(depth==1);return id==7?&s_nan_ctx.own_svc[0]:NULL;}
static struct peer_svc_info *nan_find_peer_svc(uint8_t own,uint8_t peer,const uint8_t *mac){
 assert(depth==1);return peer_found&&own==7&&peer==3&&mac[0]==2?&peer_service:NULL;
}
static struct ndl_info *nan_find_ndl(uint8_t id,const uint8_t *peer){
 assert(depth==1);return host.publisher_id&&(!id||id==host.ndp_id)&&MACADDR_EQUAL(peer,host.peer)?&host:NULL;
}
static struct ndl_info *nan_find_ndl_by_pub_id_and_peer(uint8_t pub,const uint8_t *peer){
 struct ndl_info *value=nan_find_ndl(0,peer);return value&&value->publisher_id==pub?value:NULL;
}
static bool ndl_limit_reached(void){assert(depth==1);return host.publisher_id!=0;}
static void nan_record_new_ndl(uint8_t id,uint8_t pub,const uint8_t *peer,uint8_t role,uint32_t caps){
 assert(depth==1&&!host.publisher_id);host=(struct ndl_info){.ndp_id=id,.publisher_id=pub,.own_role=role,.device_caps=caps};
 MACADDR_COPY(host.peer,peer);
}
static bool nan_is_datapath_active(void){assert(depth==1);return host.publisher_id!=0;}
static void forced_memzero(void *p,size_t n){assert(depth==1);memset(p,0,n);}
static int esp_wifi_get_mac(int interface,uint8_t *mac){assert(!depth&&!locked&&interface==WIFI_IF_NAN);memcpy(mac,"\2\3\4\5\6\10",6);return 0;}
static void esp_wifi_nan_get_ipv6_linklocal_from_mac(ip6_addr_t *ip,const uint8_t *mac){assert(ip&&mac);memset(ip,0,sizeof(*ip));}
static void esp_netif_action_disconnected(esp_netif_t *n,int base,int event,void *data){
 assert(!depth&&!locked&&n==&netif&&base==WIFI_EVENT&&event==WIFI_EVENT_NDP_TERMINATED&&!data);++disconnected;
}
static void esp32qjs_nan_control(esp32_mquickjs_wifi_nan_sdk_notice_kind_t kind,uint8_t service,
 uint8_t id,int status,uint32_t context,const uint8_t *peer){
 esp32_mquickjs_wifi_nan_sdk_notice_t notice={.kind=kind,.service_id=service,.ndp_id=id,.status=status,.context=context};
 MACADDR_COPY(notice.peer,peer);esp32_mquickjs_wifi_nan_ndp_notice(&notice);
}
static int esp_nan_internal_datapath_req(wifi_nan_datapath_req_t *,uint8_t *,uint8_t *);
static int esp_nan_internal_datapath_resp(wifi_nan_datapath_resp_t *,uint8_t *);
static int esp_nan_internal_datapath_end(wifi_nan_datapath_end_req_t *);
