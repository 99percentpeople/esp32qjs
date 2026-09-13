
int main(void){
 setup(true);incoming();tick();
 esp32_mquickjs_future_driver_state_t state={.operation=NAN_PATH_RECEIVE,.discovery=d,.timeout_ms=10};
 assert(nan_start(NULL,NULL,0,&state)&&state.path);
 assert(nan_poll(&state)==ESP32_MQUICKJS_FUTURE_READY&&!state.path->status.delivered);
 assert(nan_cancel_wait(&state)==ESP32_MQUICKJS_CANCELLED);
 esp32_mquickjs_wifi_nan_path_unreserve(state.path);esp32_mquickjs_wifi_nan_path_release(state.path);state.path=NULL;
 esp32_mquickjs_wifi_nan_path_t*p=NULL;assert(!esp32_mquickjs_wifi_nan_path_receive(d,&p)&&p);
 assert(esp32_mquickjs_wifi_nan_path_deliver(p));
 state=(esp32_mquickjs_future_driver_state_t){.operation=NAN_PATH_READY,.path=p,.timeout_ms=10};
 assert(nan_start(NULL,NULL,0,&state));assert(nan_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING);
 assert(nan_on_timeout(NULL,&state,10)==-1&&wait_timeout_seen&&!p->status.closing);
 assert(nan_cancel_wait(&state)==ESP32_MQUICKJS_CANCELLED&&!p->status.closing);
 esp32_mquickjs_wifi_nan_path_close(p,false);tick();
 assert(path_responses==1&&!path_response_accept);
 path_native[0].native_deleted=path_native[0].frames_retired=true;tick();
 esp32_mquickjs_wifi_nan_path_release(p);finish();
 /* Exercise shared extracted lifecycle utilities with no external owner. */
 assert(nan_timeout(&state)==10);nan_destroy(NULL);
}
