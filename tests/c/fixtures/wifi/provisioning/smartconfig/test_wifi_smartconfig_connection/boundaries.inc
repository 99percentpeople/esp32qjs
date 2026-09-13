
#define ESP_ERR_NO_MEM -8
#define ESP_ERR_NOT_FINISHED -9
static struct {unsigned identity;} s_wifi_lifecycle;
typedef struct {
 uint32_t generation,terminal;int32_t reason;bool connected,draining;
} esp32_mquickjs_wifi_smartconfig_connection_status_t;
typedef struct {
 uint32_t generation,kind;int32_t reason;int64_t completed_us;
 esp32_mquickjs_wifi_link_snapshot_t link;
} esp32_mquickjs_wifi_connect_event_t;
static esp32_mquickjs_wifi_radio_operation_t borrowed;
static unsigned borrow_calls,release_calls,submit_calls;
static int borrow_error,submit_error,release_error;
static esp_err_t esp32_mquickjs_wifi_radio_smartconfig_connection_begin(const esp32_mquickjs_wifi_radio_operation_t *owner){
 assert(!lock_depth && !borrowed.identity);borrow_calls++;
 if(borrow_error)return borrow_error;borrowed=*owner;return 0;
}
static esp_err_t esp32_mquickjs_wifi_radio_smartconfig_connection_end(const esp32_mquickjs_wifi_radio_operation_t *owner){
 assert(!lock_depth && !memcmp(&borrowed,owner,sizeof(*owner)));release_calls++;
 if(release_error)return release_error;memset(&borrowed,0,sizeof(borrowed));return 0;
}
static esp_err_t wifi_start_connect_reserved(const wifi_config_t *config,uint32_t timeout){
 assert(!lock_depth && borrowed.identity && config->sta.pmf_cfg.capable && timeout);
 submit_calls++;if(!submit_error)s_wifi_state.connect_in_progress=true;return submit_error;
}
/* Unrelated scan is idle; submitting one is unavailable in this fixture. */
esp_err_t esp32_mquickjs_wifi_drain_scan(void){assert(!s_wifi_state.scan_in_progress && !s_wifi_state.scan_draining);return ESP_OK;}
static int esp32_mquickjs_wifi_start_scan(const wifi_scan_config_t *c,uint32_t generation){(void)c;(void)generation;assert(0);return ESP_FAIL;}
