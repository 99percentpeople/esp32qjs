
static unsigned nan_releases,ipc_posts;
static int ipc_error,nan_error;
static esp_err_t esp_wifi_action_tx_req(wifi_action_tx_req_t *request);
static esp_err_t esp32_mquickjs_wifi_nan_sdk_ndp_release_native(uint32_t id,
 esp32_mquickjs_wifi_nan_ndp_status_t *status){
 assert(ioctl_task&&id==19);++nan_releases;
 *status=(esp32_mquickjs_wifi_nan_ndp_status_t){.identity=id,.native_deleted=true,.frames_retired=true};
 return nan_error;
}
