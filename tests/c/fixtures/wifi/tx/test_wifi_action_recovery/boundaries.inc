
#include <stdatomic.h>
#define RADIO_EVENTS_STOP 2
#define AP_STOP_IDLE 0
#define WIFI_EVENT 4
#define ESP_EVENT_ANY_ID -1
#define ESP_ERR_WIFI_NOT_STARTED 101
#define portMAX_DELAY UINT32_MAX
/* Raw TX recovery is outside this fixture; exact guards cannot grant it. */
static struct {struct {unsigned generation,identity,radio_lease_identity;} operation;bool stopped,sdk_fenced;} s_raw_tx_recovery;
static bool wifi_radio_raw_tx_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {(void)token;return false;}
static bool wifi_radio_raw_tx_recovery_owner_locked(const esp32_mquickjs_wifi_radio_lease_t *lease) {(void)lease;return false;}
static int esp32_mquickjs_wifi_raw_tx_broker_quiesce(unsigned generation,const void *token) {(void)generation;(void)token;assert(!"unexpected Raw TX recovery");return -1;}
static struct {void *snapshot;} s_config_restart;
static struct {esp32_mquickjs_wifi_radio_lifecycle_t owner;} s_policy_restart;
static struct {uint32_t generation;} s_inactive_history;
static struct {uint32_t generation;} s_scan_parameters;
static unsigned s_tx_rates,s_policies;
static void *s_channel_event_instance;
static atomic_uint s_channel_callbacks;
static unsigned stop_calls,stop_waits,deinit_calls,unregister_calls;
static int stop_error,wait_error,deinit_error,unregister_error;
static bool wait_expired;
typedef struct {bool unregister_written;} esp32_mquickjs_wifi_vendor_ie_broker_status_t;
typedef struct {uint32_t generation;} esp32_mquickjs_wifi_raw_tx_broker_status_t;
static void wifi_radio_set_state(esp32_mquickjs_wifi_radio_driver_state_t value) {assert(locks && !critical);s_radio.driver_state=value;}
static int wifi_radio_begin_events(unsigned phase,wifi_mode_t mode) {(void)mode;assert(locks && !critical);s_radio.event_phase=phase;return 0;}
static void wifi_radio_capture_stop_snapshot_locked(void) {assert(locks && !critical);}
static int wifi_radio_wait_events(void) {assert(locks && !critical);++stop_waits;if(!wait_error)s_radio.event_phase=RADIO_EVENTS_IDLE;return wait_error;}
static unsigned esp32_mquickjs_wifi_wait_remaining(unsigned ticks) {assert(locks && !critical);return wait_expired?0:ticks;}
static void vTaskDelay(unsigned ticks) {assert(ticks==1 && locks && !critical);atomic_store(&s_channel_callbacks,0);}
static int esp_wifi_stop(void) {assert(locks && !critical && s_action.lease.acquired);++stop_calls;return stop_error;}
#undef esp_wifi_deinit
static int esp_wifi_deinit(void) {
    assert(locks && !critical && !s_radio.stop_required && !s_channel_event_instance);
    assert(s_action.lease.acquired && s_radio.operation.identity);++deinit_calls;return deinit_error;
}
#define esp_wifi_deinit(...) WIFI_RADIO_MUTATION(esp_wifi_deinit(__VA_ARGS__))
static int esp_event_handler_instance_unregister(int base,int id,void *instance) {
    assert(locks && !critical && base==WIFI_EVENT && id==ESP_EVENT_ANY_ID && instance);
    ++unregister_calls;return unregister_error;
}
static int esp32_mquickjs_wifi_vendor_ie_broker_unregister(unsigned generation) {assert(locks && !critical && generation);return 0;}
static void esp32_mquickjs_wifi_vendor_ie_broker_status(esp32_mquickjs_wifi_vendor_ie_broker_status_t *out) {*out=(esp32_mquickjs_wifi_vendor_ie_broker_status_t){0};}
static int esp32_mquickjs_wifi_vendor_ie_broker_reset(unsigned generation) {assert(locks && !critical && generation);return 0;}
static void esp32_mquickjs_wifi_raw_tx_broker_status(esp32_mquickjs_wifi_raw_tx_broker_status_t *out) {*out=(esp32_mquickjs_wifi_raw_tx_broker_status_t){0};}
static int esp32_mquickjs_wifi_raw_tx_broker_unregister(unsigned generation) {(void)generation;assert(!"unexpected Raw TX ownership");return 0;}
static bool esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(unsigned generation) {(void)generation;assert(!"unexpected Raw TX generation");return false;}
static int esp32_mquickjs_wifi_interval_invalidate(void *storage,unsigned generation) {assert(storage==&s_interval && generation && locks && !critical);return 0;}
static void esp32_mquickjs_wifi_tx_rate_invalidate(void *storage) {assert(storage==&s_tx_rates && locks && !critical);}
static void esp32_mquickjs_wifi_policy_invalidate(void *storage) {assert(storage==&s_policies && locks && !critical);}
static void wifi_radio_restart_configs_discard_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {assert(token && !s_config_restart.snapshot && locks && !critical);}
