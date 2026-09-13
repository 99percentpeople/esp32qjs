
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
