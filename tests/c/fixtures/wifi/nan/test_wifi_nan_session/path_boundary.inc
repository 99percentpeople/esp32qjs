
/* Inject protocol outcomes at the Radio/ledger boundary. The complete
 * production Session/path worker and ownership transfer run above it. */
static esp32_mquickjs_wifi_nan_ndp_status_t path_native[2];
static uint32_t path_next_native=100;
static unsigned path_requests,path_responses,path_ends,path_releases;
static int path_request_error,path_response_error,path_end_error,path_release_error;
static bool path_inline,path_response_accept;
static esp32_mquickjs_wifi_nan_ndp_status_t *path_find(uint32_t id){
 for(unsigned i=0;i<2;i++){if(id&&path_native[i].identity==id)return &path_native[i];}
 return NULL;
}
bool esp32_mquickjs_wifi_nan_ndp_status(uint32_t id,esp32_mquickjs_wifi_nan_ndp_status_t*out){
 assert(!lock_depth);esp32_mquickjs_wifi_nan_ndp_status_t*p=path_find(id);
 if(!p||!native_started)return false;
 *out=*p;return true;
}
unsigned esp32_mquickjs_wifi_nan_ndp_list(esp32_mquickjs_wifi_nan_ndp_status_t out[ESP_WIFI_NAN_DATAPATH_MAX_PEERS]){
 assert(!lock_depth);unsigned count=0;
 if(native_started){for(unsigned i=0;i<2;i++){if(path_native[i].identity)out[count++]=path_native[i];}}
 return count;
}
bool esp32_mquickjs_wifi_nan_ndp_ssi(uint32_t id,uint8_t out[ESP_WIFI_MAX_SVC_SSI_LEN],uint16_t*length){
 assert(!lock_depth&&out&&length);if(!path_find(id))return false;*length=3;memcpy(out,"ndp",3);return true;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_request(const esp32_mquickjs_wifi_radio_operation_t*t,
 uint8_t service,wifi_nan_datapath_req_t*request,uint32_t*identity){
 assert(!lock_depth&&t->identity&&service==7&&request->pub_id==3&&identity&&!*identity);++path_requests;
 if(path_request_error)return path_request_error;
 assert(!path_native[0].identity);*identity=path_next_native++;
 path_native[0]=(esp32_mquickjs_wifi_nan_ndp_status_t){.identity=*identity,.service_id=service,
  .publisher_id=3,.ndp_id=9,.native_bound=true,.host_bound=true,.outgoing=true,.submitted=true,
  .started_us=clock_us,.accepted=path_inline,.completed_us=path_inline?clock_us:0};
 memcpy(path_native[0].peer,request->peer_mac,6);return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_response(const esp32_mquickjs_wifi_radio_operation_t*t,
 uint32_t id,bool accept,const uint8_t*ssi,uint16_t length){
 assert(!lock_depth&&t->identity&&(!length||ssi));++path_responses;path_response_accept=accept;
 esp32_mquickjs_wifi_nan_ndp_status_t*p=path_find(id);assert(p&&!p->response_attempted);
 p->response_attempted=true;return path_response_error;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_end(const esp32_mquickjs_wifi_radio_operation_t*t,uint32_t id){
 assert(!lock_depth&&t->identity);++path_ends;esp32_mquickjs_wifi_nan_ndp_status_t*p=path_find(id);
 assert(p&&!p->end_attempted);p->end_attempted=true;p->end_submitted=!path_end_error;return path_end_error;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_release(const esp32_mquickjs_wifi_radio_operation_t*t,
 uint32_t id,esp32_mquickjs_wifi_nan_ndp_status_t*out){
 assert(!lock_depth&&t->identity&&out);++path_releases;
 if(path_release_error)return path_release_error;
 esp32_mquickjs_wifi_nan_ndp_status_t*p=path_find(id);assert(p);
 if(!p->native_deleted||!p->frames_retired)return ESP_ERR_TIMEOUT;
 *out=*p;memset(p,0,sizeof(*p));return 0;
}
