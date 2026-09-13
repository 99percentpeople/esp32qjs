
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
typedef int esp_err_t;
typedef struct{uint32_t identity;bool acquired;} esp32_mquickjs_wifi_radio_lease_t;
static esp32_mquickjs_wifi_radio_lease_t s_wifi_application={1,true},ap={3,true};
static struct {
 struct{bool connected;}status;
 bool connect_in_progress,connect_draining,connect_start_active,disconnect_active;
 bool scan_in_progress,scan_draining,scan_results_pending,scan_start_active,scan_stop_active;
 bool scan_future_registered,connect_future_registered,runtime_cleanup_pending;
 const char *cleanup_stage;
 unsigned connect_generation;
 esp32_mquickjs_wifi_radio_lease_t radio_lease;
}s_wifi_state={.radio_lease={2,true}};
static struct{struct{uint32_t identity;}owner;}s_wifi_smartconfig_connect;
static struct{uint32_t identity;}s_wifi_lifecycle;
static bool s_wifi_configuration_cleanup,s_wifi_ap_stop_cleanup,locked;
static uint32_t s_wifi_wps_capture_identity,s_wifi_wps_ap_helper_identity,s_wifi_wps_ap_next_helper_identity=1;
static void wifi_lock(void){assert(!locked);locked=true;}
static void wifi_unlock(void){assert(locked);locked=false;}
static const esp32_mquickjs_wifi_radio_lease_t *esp32_mquickjs_wifi_ap_control_lease(void){return ap.acquired?&ap:0;}
