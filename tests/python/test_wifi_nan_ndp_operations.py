"""Deferred checked NDP submits using the production SDK helpers and ledger.

The native driver, host lookup, netif and data mutex are injected boundaries.
These cases do not establish native RX/timer retirement or RF acceptance.
"""
import unittest

from test_wifi_nan_tx import NanTx, BASE


class NanDatapathOperations(unittest.TestCase):
    compile_case = NanTx.compile_case

    def helpers(self):
        return BOUNDARY + (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_sdk_ndp.inc').read_text() + NATIVE

    def test_release_clears_failed_preclaim_and_reuses_deleted_native_address(self):
        self.compile_case(r'''
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 wifi_nan_datapath_req_t req={.pub_id=3,.peer_mac={2,3,4,5,6,7}};
 uint32_t failed=0;esp32_mquickjs_wifi_nan_ndp_status_t state;
 request_error=-73;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_request(7,&req,&failed)==-73&&failed&&host.publisher_id);
 host.own_role=ESP_WIFI_NDP_ROLE_RESPONDER;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_release_native(failed,&state)==ESP_ERR_INVALID_STATE);
 assert(host.publisher_id&&esp32_mquickjs_wifi_nan_ndp_service_busy(7));
 host.own_role=ESP_WIFI_NDP_ROLE_INITIATOR;
 assert(!esp32_mquickjs_wifi_nan_sdk_ndp_release_native(failed,&state));
 assert(state.error==-73&&state.frames_retired&&!host.publisher_id);
 assert(!esp32_mquickjs_wifi_nan_ndp_service_busy(7));
 request_error=0;uint32_t first=0,second=0;s_ndp_id=255;
 assert(!esp32_mquickjs_wifi_nan_sdk_ndp_request(7,&req,&first));
 assert(first!=failed&&host.ndp_id==255&&!s_ndp_id);
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_release_native(first,&state)==ESP_ERR_TIMEOUT);
 esp32qjs_nan_ndp_delete(native_ndl);
 assert(!esp32_mquickjs_wifi_nan_sdk_ndp_release_native(first,&state));
 assert(!esp32_mquickjs_wifi_nan_sdk_ndp_request(7,&req,&second));
 assert(second!=first&&host.ndp_id==1&&s_ndp_id==2);
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_release_native(first,&state)==ESP_ERR_INVALID_STATE);
 assert(host.ndp_id==1);esp32qjs_nan_ndp_delete(native_ndl);
 assert(!esp32_mquickjs_wifi_nan_sdk_ndp_release_native(second,&state));
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live&&!depth);
}
''', native=self.helpers())

    def test_request_binds_before_early_confirm_and_preserves_native_failure(self):
        self.compile_case(r'''
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 wifi_nan_datapath_req_t req={.pub_id=3,.peer_mac={2,3,4,5,6,7}};
 uint32_t identity=0;esp32_mquickjs_wifi_nan_ndp_status_t state;
 peer_found=false;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_request(7,&req,&identity)==ESP_ERR_NOT_FOUND&&!identity&&!request_calls);
 peer_found=true;
 assert(!esp32_mquickjs_wifi_nan_sdk_ndp_request(7,&req,&identity)&&identity&&request_calls==1&&!depth);
 assert(esp32_mquickjs_wifi_nan_ndp_status(identity,&state)&&state.accepted&&state.host_bound&&state.submitted);
 assert(state.ndp_id==9&&host.ndp_id==9&&host.publisher_id==3);
 assert(!esp32qjs_nan_ndp_peer_service(req.peer_mac,3,&s_nan_ctx.own_svc[1]));
 assert(esp32qjs_nan_ndp_peer_service(req.peer_mac,3,&s_nan_ctx.own_svc[0]));
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_request(7,&req,&identity)==ESP_ERR_INVALID_ARG&&request_calls==1);
 esp32qjs_nan_ndp_delete(native_ndl);
 assert(delete_calls==1&&!host.publisher_id&&disconnected==1&&!depth);
 assert(esp32_mquickjs_wifi_nan_ndp_status(identity,&state)&&state.native_deleted);
 assert(!esp32_mquickjs_wifi_nan_tx_close());
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 identity=0;request_error=-73;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_request(7,&req,&identity)==-73&&identity&&request_calls==2);
 assert(esp32_mquickjs_wifi_nan_ndp_status(identity,&state)&&state.error==-73&&!state.submitted);
 assert(host.publisher_id==3); /* failed submit cannot prove native retirement */
 assert(!esp32_mquickjs_wifi_nan_tx_close());
}
''', native=self.helpers())

    def test_incoming_response_and_end_preserve_identity_and_submit_errors(self):
        self.compile_case(r'''
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 s_nan_ctx.own_svc[0].type=ESP_NAN_PUBLISH;
 memcpy(native_ndl,"\2\3\4\5\6\7",6);void *ndp=NULL;
 assert(!esp32qjs_nan_ndp_alloc(native_ndl,9,&ndp));
 uint32_t identity=esp32_mquickjs_wifi_nan_ndp_deleting(native_ndl);assert(identity);
 NAN_DATA_LOCK();nan_record_new_ndl(9,7,native_ndl,ESP_WIFI_NDP_ROLE_RESPONDER,0);NAN_DATA_UNLOCK();
 esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_REQUEST,7,9,1,0,native_ndl);
 uint8_t ssi[2]={4,5};response_error=-74;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_response(identity,true,ssi,513)==ESP_ERR_INVALID_ARG&&!response_calls);
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_response(identity,true,ssi,2)==-74&&response_calls==1&&!depth);
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_response(identity,true,ssi,2)==ESP_ERR_INVALID_STATE&&response_calls==1);
 end_error=-75;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_end(identity)==-75&&end_calls==1&&!depth);
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_end(identity)==ESP_ERR_INVALID_STATE&&end_calls==1);
 esp32_mquickjs_wifi_nan_ndp_status_t state;
 assert(esp32_mquickjs_wifi_nan_ndp_status(identity,&state)&&state.error==-74&&state.cleanup_error==-75);
 esp32qjs_nan_ndp_delete(native_ndl);
 assert(!esp32_mquickjs_wifi_nan_tx_close());
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_end(identity)==ESP_ERR_INVALID_STATE&&end_calls==1);
 assert(!esp32_mquickjs_wifi_nan_tx_close());
}
''', native=self.helpers())


BOUNDARY = r'''
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
'''

NATIVE = r'''
int nan_dp_alloc_ndp(void *ndl,uint8_t id,void **ndp){assert(!depth&&!locked&&ndl==native_ndl&&id);native_ndl[9]=id;*ndp=native_ndl+8;return 0;}
void nan_dp_delete_peer(void *ndl){
 assert(!depth&&!locked&&ndl==native_ndl&&host.publisher_id);++delete_calls;memset(ndl,0,580);
}
void *nan_get_peer_svc_record(const uint8_t *peer,uint8_t id,void *service){assert(peer&&id==3&&service==&s_nan_ctx.own_svc[0]);return &peer_service;}
int __real_nan_datapath_send_req(wifi_nan_datapath_req_t *req,uint8_t *id,uint8_t *ipv6){
 assert(!depth&&!locked&&ipv6&&req&&id&&!*id);++request_calls;
 if(request_error)return request_error;
 MACADDR_COPY(native_ndl,req->peer_mac);void *ndp=NULL;
 uint8_t assigned=s_ndp_id++;
 assert(!esp32qjs_nan_ndp_alloc(native_ndl,assigned,&ndp)&&host.ndp_id==assigned);
 esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_ACCEPTED,0,assigned,1,0,req->peer_mac);
 *id=assigned;return 0;
}
static int esp_nan_internal_datapath_req(wifi_nan_datapath_req_t *req,uint8_t *id,uint8_t *ipv6){
 return __wrap_nan_datapath_send_req(req,id,ipv6)?-1:0;
}
static int esp_nan_internal_datapath_resp(wifi_nan_datapath_resp_t *resp,uint8_t *ipv6){
 assert(!depth&&!locked&&ipv6&&resp->ndp_id==9&&MACADDR_EQUAL(resp->peer_mac,host.peer));
 assert(resp->accept&&resp->ssi_len==2&&resp->ssi[0]==4&&resp->ssi[1]==5);++response_calls;return response_error;
}
static int esp_nan_internal_datapath_end(wifi_nan_datapath_end_req_t *end){
 assert(!depth&&!locked&&end->ndp_id==9&&MACADDR_EQUAL(end->peer_mac,host.peer));++end_calls;return end_error;
}
'''
