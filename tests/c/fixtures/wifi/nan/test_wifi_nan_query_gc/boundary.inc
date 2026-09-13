
typedef struct{unsigned references;}esp32_mquickjs_wifi_nan_session_t;
static esp32_mquickjs_wifi_nan_session_t parent={.references=1};
static unsigned queries;
static int query_error;
static esp32_mquickjs_wifi_nan_session_t*nan_receiver(JSContext*ctx,JSValue value){(void)ctx;(void)value;return &parent;}
static bool esp32_mquickjs_wifi_nan_session_retain(esp32_mquickjs_wifi_nan_session_t*s){++s->references;return true;}
static void esp32_mquickjs_wifi_nan_session_release(esp32_mquickjs_wifi_nan_session_t*s){assert(s->references==2);--s->references;}
static JSValue nan_error(JSContext*ctx,const char*operation,esp32_mquickjs_wifi_nan_session_t*s,esp_err_t e,bool timeout){
 assert(s==&parent&&s->references==2&&e&&!timeout&&operation);return JS_ThrowInternalError(ctx,"native query failure");
}
static esp_err_t esp32_mquickjs_wifi_nan_session_query(esp32_mquickjs_wifi_nan_session_t*s,
 const esp32_mquickjs_wifi_nan_query_t*q,esp32_mquickjs_wifi_nan_query_result_t*out){
 assert(s==&parent&&s->references==2);++queries;
 assert(!q->service_name||!strcmp(q->service_name,"alpha"));
 if(q->kind==ESP32_MQUICKJS_NAN_QUERY_PEER)assert(!memcmp(q->peer,(uint8_t[6]){2,3,4,5,6,7},6));
 if(query_error)return query_error;
 *out=(esp32_mquickjs_wifi_nan_query_result_t){.service_id=7,.peer_count=15};strcpy(out->service_name,"alpha");
 for(unsigned i=0;i<15;i++)out->peers[i]=(struct nan_peer_record){.own_svc_id=7,.peer_svc_id=i+1,
  .peer_svc_type=ESP_NAN_PUBLISH,.peer_nmi={2,3,4,5,6,7},.ndp_id=i%2?0:9,.peer_ndi={2,9,8,7,6,5}};
 return ESP_OK;
}
