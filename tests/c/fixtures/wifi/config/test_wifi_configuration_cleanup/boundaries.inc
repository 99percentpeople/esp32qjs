
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -20
#define ESP_ERR_TIMEOUT -21
typedef int esp_err_t,wifi_mode_t;
typedef struct {int mode;bool allow_disconnect;} esp32_mquickjs_wifi_radio_configuration_selection_t;
typedef int esp32_mquickjs_wifi_radio_config_controls_t,esp32_mquickjs_wifi_radio_start_controls_t;
static bool selection_rejected,selection_allow_disconnect;
#define WIFI_MODE_NULL 0
#define WIFI_MODE_AP 2
static wifi_mode_t s_wifi_configuration_mode;
bool esp32_mquickjs_wifi_configuration_pending(void);
/* Independent AP/configuration scenarios; shared removal has its own fixture. */
static bool esp32_mquickjs_wifi_ap_stop_pending(void) { return false; }
static int esp32_mquickjs_wifi_stop_ap_shared(bool *handled) { *handled=false;return ESP_OK; }
esp_err_t esp32_mquickjs_wifi_cleanup_ap_configuration(void);
typedef struct { unsigned generation,identity;bool acquired; } esp32_mquickjs_wifi_radio_lease_t;
typedef struct { unsigned generation,identity; } esp32_mquickjs_wifi_radio_lifecycle_t;
static int vendor_clear_error;
static int esp32_mquickjs_wifi_radio_vendor_ie_clear_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(token && token->identity);return vendor_clear_error;
}
static esp32_mquickjs_wifi_radio_lease_t s_wifi_application,s_ap_lease;
static esp32_mquickjs_wifi_radio_lifecycle_t s_wifi_lifecycle,s_ap_lifecycle,s_ap_coordinator,native_token;
static bool s_wifi_configuration_cleanup,s_ap_cleanup_pending,s_wifi_configuration_stop_only,s_wifi_configuration_disconnect_pending;
static bool expect_stop_only;
static bool esp32_mquickjs_wifi_radio_ap_deauth_poll(bool *pending) { *pending=false;return false; }
static int wifi_finish_configuration_disconnect(void) { assert(!s_wifi_configuration_disconnect_pending);return 0; }
static void *s_ap_netif;
static struct {uint32_t generation,identity;} s_ap_raw_owner;
static int s_ap_detach_error;
static const char *s_ap_stage;
static struct {
    esp32_mquickjs_wifi_radio_lease_t radio_lease;
    struct { bool connected; } status;
    bool connect_in_progress,connect_draining,connect_start_active,disconnect_active;
    bool scan_in_progress,scan_draining,scan_results_pending,scan_start_active,scan_stop_active;
    bool connect_future_registered,scan_future_registered,runtime_cleanup_pending;
    const char *cleanup_stage;int cleanup_error;
} s_wifi_state;
static int fail_at,begin_calls,owner_releases,stop_calls,ap_calls,sta_calls,shutdown_calls;
static bool foreign_owner,running,sta_storage;
static bool recovery_admitted,recovery_owner_pending;
static unsigned recovery_shutdown_calls;
static void wifi_lock(void) {}
static void wifi_unlock(void) {}
static void wifi_release_radio_operation(void) {}
static bool exact(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    return t && t->identity && t->identity==native_token.identity && t->generation==native_token.generation;
}
static int esp32_mquickjs_wifi_radio_begin_lifecycle(
    const esp32_mquickjs_wifi_radio_lease_t *app,const esp32_mquickjs_wifi_radio_lease_t *sta,
    const esp32_mquickjs_wifi_radio_lease_t *ap,esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    begin_calls++;assert(app==&s_wifi_application && sta==&s_wifi_state.radio_lease && ap==&s_ap_lease);
    assert(token==&s_wifi_lifecycle && !token->identity);
    if(foreign_owner)return ESP_ERR_INVALID_STATE;
    native_token=(esp32_mquickjs_wifi_radio_lifecycle_t){9,41};*token=native_token;return 0;
}
static int esp32_mquickjs_wifi_radio_begin_configuration_lifecycle(
    const esp32_mquickjs_wifi_radio_lease_t *app,const esp32_mquickjs_wifi_radio_lease_t *sta,
    const esp32_mquickjs_wifi_radio_lease_t *ap,esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    const int *controls,const int *start_controls,esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(!controls && !start_controls);selection_allow_disconnect=selection->allow_disconnect;
    if(selection_rejected)return ESP_ERR_INVALID_STATE;
    int err=esp32_mquickjs_wifi_radio_begin_lifecycle(app,sta,ap,token);
    if(!err)selection->mode=WIFI_MODE_AP;return err;
}
static void esp32_mquickjs_wifi_radio_release(esp32_mquickjs_wifi_radio_lease_t *owner) {
    if(owner->acquired){assert(native_token.identity);owner_releases++;memset(owner,0,sizeof(*owner));}
}
static int esp32_mquickjs_wifi_radio_quiesce_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(exact(token) && !s_wifi_application.acquired && !s_wifi_state.radio_lease.acquired && !s_ap_lease.acquired);
    if(!running)return 0;
    stop_calls++;if(fail_at==1)return -1;running=false;return 0;
}
static int esp32_mquickjs_wifi_radio_check_stopped_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *t,bool cleanup) {
    assert(cleanup);return exact(t) && !running?0:ESP_ERR_INVALID_STATE;
}
/* Recovery admission, physical SDK proof and native owner consumption are
 * Radio boundaries; the real producer is composed in test_wifi_action_recovery. */
static int esp32_mquickjs_wifi_radio_prepare_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(exact(token) && recovery_admitted);return ESP_OK;
}
static bool esp32_mquickjs_wifi_radio_recovery_active(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    return recovery_admitted && exact(token);
}
static int esp32_mquickjs_wifi_radio_check_stopped_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    return recovery_admitted && exact(token) && !running?ESP_OK:ESP_ERR_INVALID_STATE;
}
static int esp32_mquickjs_wifi_radio_stop_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(recovery_admitted);return esp32_mquickjs_wifi_radio_quiesce_lifecycle(token);
}
static int esp32_mquickjs_wifi_radio_shutdown_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(recovery_admitted && exact(token) && !running && !sta_storage && !s_ap_netif);
    ++recovery_shutdown_calls;return recovery_owner_pending?ESP_ERR_TIMEOUT:ESP_OK;
}
static int esp32_mquickjs_wifi_netif_retire(void **netif,int *error) {
    assert(native_token.identity && !running);
    if(*error)return *error;
    if(!*netif)return 0;
    ap_calls++;if(fail_at==2)return -2;
    *netif=NULL;return 0;
}
static int wifi_cleanup_helper(bool finish) {
    assert(!finish && native_token.identity && !running && !s_ap_netif);
    if(!sta_storage)return 0;
    sta_calls++;if(fail_at==3){s_wifi_state.cleanup_stage="netif-detach";return -3;}
    sta_storage=false;memset(&s_wifi_state,0,sizeof(s_wifi_state));return 0;
}
static int esp32_mquickjs_wifi_radio_finish_lifecycle(esp32_mquickjs_wifi_radio_lifecycle_t *token,bool shutdown) {
    assert(exact(token) && shutdown==!expect_stop_only && !running && !sta_storage && !s_ap_netif);
    shutdown_calls++;if(fail_at==4)return -4;
    memset(token,0,sizeof(*token));memset(&native_token,0,sizeof(native_token));return 0;
}
