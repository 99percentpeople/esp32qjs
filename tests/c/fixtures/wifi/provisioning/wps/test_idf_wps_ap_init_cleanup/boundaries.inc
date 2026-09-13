
#define ETH_ALEN 6
struct hostapd_data{int unused;};
static int releases;
static int wifi_ap_wps_deinit(void){
 assert(s_wps_ap_result && s_wps_ap_result->status.close_prepared);
 assert(s_wps_ap_result->status.error==-81 && !s_wps_ap_result->status.retained);
 releases++;
 esp32qjs_wps_ap_result_detach(s_wps_ap_result->context,ESP_OK);
 return ESP_OK;
}
