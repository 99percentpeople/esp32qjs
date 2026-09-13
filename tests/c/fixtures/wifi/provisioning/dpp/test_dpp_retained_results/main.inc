
static int close_id(uint64_t id){return esp_dpp_deinit((void*)(uintptr_t)(id>>32),(void*)(uintptr_t)(uint32_t)id);}
int main(void){
 uint64_t id=0;fail_alloc=1;assert(esp32qjs_dpp_result_reserve(&id)==ESP_ERR_NO_MEM&&!id&&!live);
 wps_held=true;assert(esp32qjs_dpp_result_reserve(&id)==ESP_ERR_INVALID_STATE);wps_held=false;
 assert(!esp32qjs_dpp_result_reserve(&id)&&id&&esp32qjs_dpp_result_held());
 assert(esp32qjs_dpp_result_discard_unbound(id+1)==ESP_ERR_INVALID_STATE);
 dpp_api_lock();assert(!esp32qjs_dpp_result_bind_locked(id));
 assert(!esp32qjs_dpp_result_uri_locked("DPP:K:x;;",9));
 assert(esp32qjs_dpp_result_uri_locked("DPP:K:y;;",9)==ESP_ERR_INVALID_STATE);dpp_api_unlock();
 char uri[32];assert(esp32qjs_dpp_result_uri_copy(id,uri,9)==ESP_ERR_INVALID_SIZE);
 assert(!esp32qjs_dpp_result_uri_copy(id,uri,sizeof(uri))&&!strcmp(uri,"DPP:K:x;;"));
 assert(esp32qjs_dpp_result_uri_commit(id+1)==ESP_ERR_INVALID_STATE);
 assert(!esp32qjs_dpp_result_uri_commit(id));assert(esp32qjs_dpp_result_uri_commit(id)==ESP_ERR_INVALID_STATE);
 size_t size=sizeof(wifi_event_dpp_config_received_t)+sizeof(esp_dpp_config_data_t);
 wifi_event_dpp_config_received_t*event=calloc(1,size);assert(event);event->total_conf=1;
 esp_dpp_config_data_t*row=&event->configs[0];row->ssid_len=3;memcpy(row->ssid,"abc",3);row->akm=1;
 row->connector_len=2;memcpy(row->connector,"xy",2);row->net_access_key_len=2;row->net_access_key[0]=71;
 row->c_sign_key_len=1;row->c_sign_key[0]=72;row->net_access_key_expiry=123456;
 dpp_api_lock();assert(!esp32qjs_dpp_result_configs_locked(event));esp32qjs_dpp_result_failure_locked(42);dpp_api_unlock();
 esp_dpp_config_data_t out;assert(!esp32qjs_dpp_result_config_copy(id,0,&out));assert(out.net_access_key[0]==71&&out.net_access_key_expiry==123456);
 assert(!esp32qjs_dpp_result_configs_commit(id));assert(!esp32qjs_dpp_result_config_copy(id,0,&out));
 esp32_mquickjs_wifi_dpp_result_status_t status;assert(!esp32qjs_dpp_result_status(id,&status));
 assert(status.terminal&&status.event_id==WIFI_EVENT_DPP_CFG_RECVD&&!status.error&&status.duplicate_results==1);
 assert(esp32qjs_dpp_result_release(id)==ESP_ERR_INVALID_STATE);
 s_dpp_ctx.dpp_global=(void*)1;s_dpp_ctx.dpp_config_store=(void*)2;s_dpp_init_done=true;
 s_dpp_tx_submitted=true;tx_held=true;
 s_dpp_async_active=1;assert(close_id(id)==ESP_ERR_NOT_FINISHED&&!s_dpp_result->status.closing);s_dpp_async_active=0;
 quiescent_error=44;assert(close_id(id)==44&&!heap_frees&&!unregister_fence&&cancels==1);
 quiescent_error=0;post_error=46;assert(close_id(id)==46&&!heap_frees&&!unregister_fence&&cancels==1);
 post_error=0;assert(close_id(id)==ESP_ERR_NOT_FINISHED&&post_count==2&&!heap_frees);
 uint64_t stale=id+1;esp32qjs_dpp_fence_handler(NULL,ESP32QJS_DPP_PRIVATE_EVENT,0,&stale);
 assert(close_id(id)==ESP_ERR_NOT_FINISHED&&!heap_frees&&post_count==2);
 esp32qjs_dpp_fence_handler(NULL,ESP32QJS_DPP_PRIVATE_EVENT,0,&id);
 unregister_error=45;assert(close_id(id)==45&&!heap_frees&&unregister_fence==1);
 unregister_error=0;
 assert(!close_id(id)&&heap_frees==2&&esp32qjs_dpp_result_held());
 assert(!close_id(id)&&heap_frees==2&&unregister_fence==2);assert(!esp32qjs_dpp_result_config_copy(id,0,&out));
 assert(!esp32qjs_dpp_result_release(id)&&!live&&!esp32qjs_dpp_result_held());free(event);
 uint64_t old=id;id=0;assert(!esp32qjs_dpp_result_reserve(&id)&&id>old);
 assert(esp32qjs_dpp_result_configs_commit(old)==ESP_ERR_INVALID_STATE);
 assert(!esp32qjs_dpp_result_discard_unbound(id));
 s_dpp_result_last_identity=UINT64_MAX;id=0;assert(esp32qjs_dpp_result_reserve(&id)==ESP_ERR_NO_MEM&&!live&&!depth);
 return 0;
}
