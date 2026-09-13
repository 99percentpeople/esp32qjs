
typedef int JSValue;
#define WIFI_MODE_STA 1
#define WIFI_MODE_AP 2
#define WIFI_MODE_APSTA 3
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION 3
static struct { unsigned identity; } s_wifi_lifecycle;
static bool s_wifi_configuration_cleanup,s_wifi_configuration_stop_only;
static int s_wifi_configuration_mode;
static bool ap_control,configuration_admission_denied;
static int configuration_begin_calls;
static bool wifi_helpers_idle(bool allow_connected);
static esp32_mquickjs_wifi_radio_lease_t ap_lease;
static const esp32_mquickjs_wifi_radio_lease_t *esp32_mquickjs_wifi_ap_control_lease(void) { return ap_control?&ap_lease:NULL; }
static int wifi_begin_configuration_cleanup(bool allow_disconnect) {
    configuration_begin_calls++;
    if(!wifi_helpers_idle(allow_disconnect) || configuration_admission_denied)return ESP_ERR_INVALID_STATE;
    assert(ap_control && !s_wifi_lifecycle.identity);
    s_wifi_configuration_cleanup=true;s_wifi_configuration_mode=0;s_wifi_configuration_stop_only=false;
    s_wifi_lifecycle.identity=7;ap_control=false;return 0;
}
static int configuration_cleanup_calls,configuration_cleanup_error;
static int wifi_finish_configuration_cleanup(void) {
    assert(s_wifi_configuration_cleanup);configuration_cleanup_calls++;
    if(configuration_cleanup_error)return configuration_cleanup_error;
    s_wifi_configuration_cleanup=false;s_wifi_lifecycle.identity=0;
    s_wifi_configuration_stop_only=false;s_wifi_configuration_mode=0;
    s_wifi_state.runtime_cleanup_pending=false;return 0;
}

static int init_calls,acquire_calls,begin_calls,begin_error,ensure_calls;
static int wifi_init_once(void) { init_calls++;if(s_wifi_state.runtime_cleanup_pending)return -2;s_wifi_state.initialized=true;return 0; }
static int esp32_mquickjs_wifi_radio_acquire(int client,int mode,esp32_mquickjs_wifi_radio_lease_t *lease) { assert(client==3 && mode==1);acquire_calls++;lease->acquired=true;return 0; }
static int esp32_mquickjs_wifi_radio_begin_lifecycle(const void *app,const void *sta,const void *ap,void *token) { assert(!ap && app==&s_wifi_application && sta==&s_wifi_state.radio_lease && token==&s_wifi_lifecycle);begin_calls++;if(begin_error)return begin_error;s_wifi_lifecycle.identity=1;return 0; }
static int JS_ThrowTypeError(void *ctx,const char *message) { (void)ctx;(void)message;return -98; }
static int wifi_make_status_object(void *ctx) { (void)ctx;return 123; }
#define JS_UNDEFINED -1
#define JS_EXCEPTION -99
static int wait_scope,wait_begins,wait_ends,wait_begin_error;
static bool stop_capture_error;
static bool esp32_mquickjs_wifi_capture_stop(void *ctx,int options,uint32_t *timeout) { (void)ctx;assert(options==JS_UNDEFINED);if(stop_capture_error)return false;*timeout=1000;return true; }
static int esp32_mquickjs_wifi_wait_begin(uint32_t timeout) { assert(timeout==1000 && !wait_scope);wait_begins++;if(wait_begin_error)return wait_begin_error;wait_scope=1;return 0; }
static void esp32_mquickjs_wifi_wait_end(void) { assert(wait_scope);wait_scope=0;wait_ends++; }
typedef struct { bool start_only;int mode; } esp32_mquickjs_wifi_radio_configuration_selection_t;
static int wifi_start_existing_running(const esp32_mquickjs_wifi_radio_configuration_selection_t *selection);
/* This fixture isolates public stop and the matching-running helper path.
 * Start capture/resolution/stopped execution use their own production fixtures. */
static bool esp32_mquickjs_wifi_capture_start(void *ctx,int options,esp32_mquickjs_wifi_radio_configuration_selection_t *s) {
    (void)ctx;assert(options==JS_UNDEFINED);*s=(esp32_mquickjs_wifi_radio_configuration_selection_t){true,WIFI_MODE_STA};return true;
}
static int wifi_configure_selected_interfaces(esp32_mquickjs_wifi_radio_configuration_selection_t *s,
    const void *sta,const void *accept_sta,const void *ap,const void *accept_ap,const void *controls,
    const void *start_controls,const void *execution) {
    assert(s->start_only && !sta && !accept_sta && !ap && !accept_ap && !controls && !start_controls && !execution);
    return wifi_start_existing_running(s);
}

