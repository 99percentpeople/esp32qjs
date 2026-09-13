
static bool session_locked;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!session_locked);session_locked=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(session_locked);session_locked=false;}while(0)
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n){volatile uint8_t *b=p;while(n--)*b++=0;}
static const char *esp_err_to_name(int e){(void)e;return "injected";}
esp_err_t esp32_mquickjs_wifi_dpp_worker_validate(const esp32_mquickjs_wifi_dpp_worker_options_t *o){
 assert(o&&!strcmp(o->channels,"6"));return ESP_OK;}
static esp32_mquickjs_wifi_dpp_connection_status_t current_connection;
esp_err_t esp32_mquickjs_wifi_dpp_connect_status(const esp32_mquickjs_wifi_radio_operation_t *owner,
 uint32_t generation,esp32_mquickjs_wifi_dpp_connection_status_t *out){
 assert(!session_locked&&owner->identity==13&&generation==23);*out=current_connection;return ESP_OK;}
