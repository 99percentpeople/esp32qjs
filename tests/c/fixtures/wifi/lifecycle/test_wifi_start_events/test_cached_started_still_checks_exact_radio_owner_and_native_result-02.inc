
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -1
#define WIFI_MODE_STA 1
#define WIFI_STARTED_BIT 4
#define ESP_RETURN_ON_ERROR(expr,...) do { int code=(expr);if(code)return code; } while(0)
typedef int esp_err_t;
typedef struct { bool started;unsigned mode; } esp32_mquickjs_wifi_radio_status_t;
static struct { bool started;struct { bool started; } status;int radio_lease,event_group; } s_wifi_state;
static int init_error,start_error,status_error,start_calls,status_calls,bits;
static int netif_error,netif_calls,cleanup_calls,s_wifi_setup_error;
static const char *s_wifi_setup_stage;
static esp32_mquickjs_wifi_radio_status_t observed={true,WIFI_MODE_STA};
static int wifi_init_once(void) { return init_error; }
static int esp32_mquickjs_wifi_radio_ensure_started(int *lease) {
    assert(lease==&s_wifi_state.radio_lease);start_calls++;return start_error;
}
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *status) {
    status_calls++;*status=observed;return status_error;
}
static int wifi_start_existing_station_netif(void) {
    assert(!start_error && !status_error && observed.started && observed.mode==WIFI_MODE_STA);
    netif_calls++;s_wifi_setup_stage="fixture-netif-start";return netif_error;
}
static int wifi_cleanup_failed_init(void) { cleanup_calls++;return ESP_OK; }
static void wifi_lock(void) {}
static void wifi_unlock(void) {}
static void xEventGroupSetBits(int group,unsigned value) { (void)group;bits|=value; }
