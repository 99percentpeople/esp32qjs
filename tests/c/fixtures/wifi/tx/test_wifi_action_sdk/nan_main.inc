
static esp_err_t esp_wifi_action_tx_req(wifi_action_tx_req_t *request){
 assert(!ioctl_task&&request->ifx==WIFI_IF_NAN);++ipc_posts;
 if(ipc_error)return ipc_error;
 uint8_t message[32]={0};memcpy(message+20,&request,sizeof(request));
 ioctl_task=true;esp_err_t error=__wrap_wifi_action_tx_process(message);ioctl_task=false;return error;
}
int main(void){
 esp32_mquickjs_wifi_nan_ndp_status_t state={.identity=123};
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_release(0,&state)==ESP_ERR_INVALID_ARG&&!ipc_posts);
 ipc_error=-72;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_release(19,&state)==-72&&!nan_releases&&state.identity==123);
 ipc_error=0;nan_error=-73;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_release(19,&state)==-73&&nan_releases==1&&state.identity==123);
 nan_error=0;
 assert(!esp32_mquickjs_wifi_nan_sdk_ndp_release(19,&state)&&nan_releases==2);
 assert(state.identity==19&&state.native_deleted&&state.frames_retired&&!delegates);
 action_sdk_nan_release_t wrong={.request={.ifx=WIFI_IF_STA,.type=ACTION_SDK_NAN_RELEASE_TYPE,
  .rx_cb=esp32_mquickjs_wifi_action_receive},.identity=19};
 uint8_t message[32]={0};void *request=&wrong;memcpy(message+20,&request,sizeof(request));
 ioctl_task=true;assert(__wrap_wifi_action_tx_process(message)==ESP_ERR_INVALID_ARG&&nan_releases==2);
 /* Exercise unchanged external delegation boundaries as well. */
 assert(__real_wifi_roc_process(message)==88);
 assert(!other_receive(NULL,NULL,0,0));
}
