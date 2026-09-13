
static void*allocation;
static size_t allocated_size;
static unsigned allocations,begins,bootstraps,listens,closes,releases,copies,commits;
static int ipc_mode,init_error,close_error,release_error,validate_error,select_error;
static unsigned selects;
static bool fail_alloc;
static esp32_mquickjs_wifi_dpp_result_status_t native;
static void esp32_mquickjs_wireless_secure_zero(void*p,size_t n){volatile uint8_t*q=p;while(n--)*q++=0;}
static void*heap_caps_calloc(size_t n,size_t size,unsigned caps){
 assert(!allocation&&n==1&&caps==3);if(fail_alloc)return NULL;
 allocation=calloc(n,size);assert(allocation);allocated_size=size;allocations++;return allocation;
}
static void heap_caps_free(void*p){assert(p==allocation&&allocations==1);
 for(size_t i=0;i<allocated_size;i++)assert(!((uint8_t*)p)[i]);free(p);allocation=NULL;allocations--;
}
esp_err_t esp32qjs_dpp_bootstrap_validate(const char*c,const char*k,const char*i){assert(c&&k&&i);return validate_error;}
esp_err_t esp32qjs_dpp_native_begin(uint64_t*id){assert(!*id);begins++;*id=41;
 native=(esp32_mquickjs_wifi_dpp_result_status_t){.identity=41,.attached=true,.retained=true,.reserved_bytes=999};return init_error;
}
esp_err_t esp32qjs_dpp_native_bootstrap(uint64_t id,const char*c,const char*k,const char*i){
 assert(id==41&&!strcmp(c,"1,6,11")&&!strcmp(k,"abcd")&&!strcmp(i,"label"));bootstraps++;return 0;
}
esp_err_t esp32_mquickjs_wifi_dpp_connection_prepare(const esp_dpp_config_data_t*r,esp32_mquickjs_wifi_dpp_auth_t a,
 esp32_mquickjs_wifi_dpp_auth_t*chosen,wifi_config_t*out){
 if(!r||r->secret[0]!=71)return ESP_ERR_INVALID_ARG;*chosen=a?a:ESP32_MQUICKJS_DPP_AUTH_CONNECTOR;
 memset(out,42,sizeof(*out));return 0;}
esp_err_t esp32qjs_dpp_native_select(uint64_t id,const esp_dpp_config_data_t*r){assert(id==41&&r&&r->secret[0]==71);selects++;return select_error;}
esp_err_t esp32qjs_dpp_native_check_connection(uint64_t id,uint8_t akm,const uint8_t bssid[6]){assert(id==41&&akm==1&&bssid[0]==2);return 0;}
esp_err_t esp32qjs_dpp_native_listen(uint64_t id){assert(id==41&&native.uri_length);listens++;return 0;}
esp_err_t esp32qjs_dpp_result_status(uint64_t id,esp32_mquickjs_wifi_dpp_result_status_t*out){assert(id==41);*out=native;return 0;}
esp_err_t esp32qjs_dpp_result_uri_copy(uint64_t id,char*out,size_t capacity){
 assert(id==41&&capacity>=10);if(!native.uri_available)return ESP_ERR_INVALID_STATE;memcpy(out,"DPP:K:x;;",10);copies++;return 0;
}
esp_err_t esp32qjs_dpp_result_uri_commit(uint64_t id){assert(id==41);native.uri_available=false;commits++;return 0;}
esp_err_t esp32qjs_dpp_result_config_copy(uint64_t id,unsigned index,esp_dpp_config_data_t*out){
 assert(id==41);if(index>=native.config_count)return ESP_ERR_INVALID_STATE;memset(out,71,sizeof(*out));copies++;return 0;
}
esp_err_t esp32qjs_dpp_result_configs_commit(uint64_t id){assert(id==41);native.configs_available=false;commits++;return 0;}
esp_err_t esp32qjs_dpp_native_close(uint64_t id){assert(id==41);closes++;if(close_error)return close_error;
 native.closing=true;native.sdk_retired=true;native.driver_retired=true;native.event_fenced=true;return 0;
}
esp_err_t esp32qjs_dpp_result_release(uint64_t id){assert(id==41&&native.sdk_retired);releases++;return release_error;}
