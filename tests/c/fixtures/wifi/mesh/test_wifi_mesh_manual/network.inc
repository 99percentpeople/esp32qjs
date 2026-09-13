
#define CONFIG_LWIP_DHCPS 0
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED -501
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED -502
typedef struct {int unused;} esp_netif_t;
typedef struct {struct {uint32_t addr;} ip;} esp_netif_ip_info_t;
typedef enum {ESP_NETIF_DHCP_STOPPED,ESP_NETIF_DHCP_STARTED} esp_netif_dhcp_status_t;
typedef struct {esp_netif_t *sta;esp32_mquickjs_wifi_mesh_token_t native;} wifi_radio_mesh_t;
static esp_netif_t netif;
static esp_netif_dhcp_status_t dhcp_state;
static unsigned dhcp_starts;
static bool disconnect_on_ip;
static int esp_netif_dhcpc_stop(esp_netif_t*n){assert(n==&netif);dhcp_state=ESP_NETIF_DHCP_STOPPED;return 0;}
static int esp_netif_dhcpc_start(esp_netif_t*n){assert(n==&netif);++dhcp_starts;dhcp_state=ESP_NETIF_DHCP_STARTED;return 0;}
static int esp_netif_dhcpc_get_status(esp_netif_t*n,esp_netif_dhcp_status_t*s){assert(n==&netif);*s=dhcp_state;return 0;}
static int esp_netif_dhcps_get_status(esp_netif_t*n,esp_netif_dhcp_status_t*s){(void)n;(void)s;assert(0);return 0;}
static int esp_netif_get_ip_info(esp_netif_t*n,esp_netif_ip_info_t*ip){
 assert(n==&netif);ip->ip.addr=0x0100007f;
 if(disconnect_on_ip){s_mesh->status.parent_connected=false;s_mesh->status.native_snapshot_valid=false;++s_mesh->status.events;}
 return 0;
}
static bool esp_netif_is_netif_up(esp_netif_t*n){assert(n==&netif);return true;}
