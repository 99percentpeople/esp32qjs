
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
static void *s_ap_netif;
static struct { bool acquired; } s_ap_lease;
static struct { unsigned identity; } s_ap_lifecycle, s_ap_coordinator;
static bool s_ap_cleanup_pending;
static bool deauth_pending;
static struct {unsigned identity;} s_ap_raw_owner;
static bool esp32_mquickjs_wifi_radio_ap_deauth_poll(bool *pending) { *pending=deauth_pending;return false; }
static int s_ap_detach_error;
static bool esp32_mquickjs_wifi_configuration_pending(void) { return false; }
/* Independent AP/configuration scenarios; shared removal has its own fixture. */
static bool esp32_mquickjs_wifi_ap_stop_pending(void) { return false; }
static int esp32_mquickjs_wifi_stop_ap_shared(bool *handled) { *handled=false;return ESP_OK; }
static int esp32_mquickjs_wifi_cleanup_ap_configuration(void) { assert(0);return -1; }
#define ESP_ERR_INVALID_STATE -2
static const char *s_ap_stage;
static int failed,calls[6],freed,releases;
static int boundary(int n) { calls[n]++;return failed==n ? -n:0; }
static int esp32_mquickjs_wifi_radio_begin_lifecycle(const void *a,const void *s,const void *ap,void *t) { assert(!a && !s && ap && t);int e=boundary(1);if(!e)s_ap_lifecycle.identity=1;return e; }
static void esp32_mquickjs_wifi_radio_release(void *l) { assert(l);s_ap_lease.acquired=false;releases++; }
static int esp32_mquickjs_wifi_radio_quiesce_lifecycle(void *t) { assert(t && s_ap_lifecycle.identity);return boundary(2); }
static int esp_wifi_clear_default_wifi_driver_and_handlers(void *n) { assert(n && s_ap_lifecycle.identity);return boundary(3); }
static void esp_netif_destroy(void *n) { assert(n && s_ap_lifecycle.identity);freed++; }
/* SDK-facing retirement boundary; its scheduler is covered separately by
 * test_wifi_netif_retirement using the complete production translation unit. */
static int esp32_mquickjs_wifi_netif_retire(void **n,int *error) {
    if(*error)return *error;
    if(!*n)return ESP_OK;
    *error=esp_wifi_clear_default_wifi_driver_and_handlers(*n);
    if(!*error){esp_netif_destroy(*n);*n=NULL;}
    return *error;
}
static int esp32_mquickjs_wifi_radio_finish_lifecycle(void *t,bool shutdown) { assert(t && shutdown && !s_ap_netif);int e=boundary(4);if(!e)s_ap_lifecycle.identity=0;return e; }
