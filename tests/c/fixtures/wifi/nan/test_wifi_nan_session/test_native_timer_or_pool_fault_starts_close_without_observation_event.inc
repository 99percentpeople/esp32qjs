
int main(void){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());wifi_nan_sync_config_t cfg={.op_channel=6};
 esp32_mquickjs_wifi_nan_session_t*s=NULL;assert(!esp32_mquickjs_wifi_nan_session_create(&cfg,1000,&s));
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));
 clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);assert(s->status.ready);
 native_pool_error=-73;clock_us+=100000;
 assert(esp32_mquickjs_wifi_nan_service()&&s->status.closing&&s->status.error==-73);
 run_queued(NULL);assert(s->status.retired&&!s_nan_active);
 esp32_mquickjs_wifi_nan_session_release(s);assert(!allocations);
}
