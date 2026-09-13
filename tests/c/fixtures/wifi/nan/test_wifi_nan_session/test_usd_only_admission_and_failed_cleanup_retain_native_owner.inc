
int main(void){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());
 wifi_nan_sync_config_t cfg={.op_channel=6};
 esp32_mquickjs_wifi_nan_session_t*s=NULL;
 assert(esp32_mquickjs_wifi_nan_session_create(&cfg,1000,&s)==ESP_ERR_NOT_SUPPORTED);
 assert(!s&&!allocation_calls&&!begins&&!s_nan_handles);
 fail_allocation=1;
 assert(esp32_mquickjs_wifi_nan_session_create_usd(1000,&s)==ESP_ERR_NO_MEM);
 assert(!s&&!allocations&&!s_nan_handles);
 fail_allocation=0;
 assert(!esp32_mquickjs_wifi_nan_session_create_usd(1000,&s)&&s->status.usd);
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));
 assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
 assert(s->status.ready&&s->status.native.usd&&begins==1);
 esp32_mquickjs_wifi_nan_global_status_t global;
 esp32_mquickjs_wifi_nan_global_status(&global);
 assert(!global.active_paths&&!global.path_handles);
 close_error=-77;esp32_mquickjs_wifi_nan_session_close(s,false);
 clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
 assert(!s->status.retired&&s_nan_active==s&&s->status.cleanup_error==-77);
 assert(native_started&&allocations&&begins==1);
 close_error=0;clock_us+=100000;
 assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
 assert(s->status.retired&&!s_nan_active&&!native_started);
 esp32_mquickjs_wifi_nan_session_release(s);assert(!allocations);
}
