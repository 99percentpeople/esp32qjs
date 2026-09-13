
static TickType_t xTaskGetTickCount(void) {return ticks;}
static TickType_t esp32_mquickjs_wifi_wait_remaining(TickType_t fallback){return wait_limit<fallback?wait_limit:fallback;}
static void vTaskDelay(TickType_t t) {
    assert(!critical && waits==1 && native_mode==WIFI_MODE_AP);ticks+=t;delays++;
    if(csa_pending && !never_settle && delays==2) {
        native_channel=native_home=csa_primary;native_secondary=native_home_secondary=csa_secondary;
        native_band=csa_primary>14?WIFI_BAND_5G:WIFI_BAND_2G;csa_pending=false;
    }
}
static bool esp32_mquickjs_cooperate(esp32_mquickjs_runtime_t *runtime){(void)runtime;assert(waits==1);return !cancel_wait;}
static esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void){return NULL;}
static void esp32_mquickjs_native_wait_begin(esp32_mquickjs_runtime_t *r,esp32_mquickjs_native_wait_t *w){(void)r;(void)w;assert(!waits);waits=1;}
static void esp32_mquickjs_native_wait_end(esp32_mquickjs_runtime_t *r,esp32_mquickjs_native_wait_t *w){(void)r;(void)w;assert(waits==1);waits=0;}
static int wifi_radio_ensure_started_locked(esp32_mquickjs_wifi_radio_lease_t *lease){(void)lease;assert(!"AP activation must use its production path");return ESP_ERR_INVALID_STATE;}
