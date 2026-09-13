
/* Isolated no-Vendor-IE module boundary. */
#define WIFI_RADIO_MESH_PENDING false
static struct {esp32_mquickjs_wifi_radio_lifecycle_t start_owner;} s_vendor_ie;
static void wifi_radio_restart_configs_discard_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {(void)token;} /* Config checkpoint covered separately. */
static int cleanup_error;static unsigned cleanups;
static int wifi_radio_shutdown_locked(void) {assert(locks==1 && !critical);++cleanups;return cleanup_error;}
static int wifi_radio_stop_locked(void) {assert(locks==1 && !critical);++cleanups;return cleanup_error;}
