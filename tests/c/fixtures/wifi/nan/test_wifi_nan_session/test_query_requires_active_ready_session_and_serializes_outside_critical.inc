
int main(void){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());wifi_nan_sync_config_t cfg={.op_channel=6};
 esp32_mquickjs_wifi_nan_session_t*s=NULL;assert(!esp32_mquickjs_wifi_nan_session_create(&cfg,1000,&s));
 esp32_mquickjs_wifi_nan_query_t q={.kind=ESP32_MQUICKJS_NAN_QUERY_SERVICE,.service_id=7};
 esp32_mquickjs_wifi_nan_query_result_t out;
 assert(esp32_mquickjs_wifi_nan_session_query(s,&q,&out)==ESP_ERR_INVALID_STATE&&!query_reads);
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));
 clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);assert(s->status.ready);
 assert(!esp32_mquickjs_wifi_nan_session_query(s,&q,&out)&&query_reads==1&&out.service_id==7);
 s->status.usd=true;assert(esp32_mquickjs_wifi_nan_session_query(s,&q,&out)==ESP_ERR_NOT_SUPPORTED&&query_reads==1);
 s->status.usd=false;s_nan_runtime_closing=true;
 assert(esp32_mquickjs_wifi_nan_session_query(s,&q,&out)==ESP_ERR_INVALID_STATE&&query_reads==1);
 s_nan_runtime_closing=false;esp32_mquickjs_wifi_nan_session_close(s,false);
 assert(esp32_mquickjs_wifi_nan_session_query(s,&q,&out)==ESP_ERR_INVALID_STATE&&query_reads==1);
 clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);assert(s->status.retired);
 assert(esp32_mquickjs_wifi_nan_session_query(s,&q,&out)==ESP_ERR_INVALID_STATE&&query_reads==1);
 esp32_mquickjs_wifi_nan_session_release(s);assert(!allocations);
}
