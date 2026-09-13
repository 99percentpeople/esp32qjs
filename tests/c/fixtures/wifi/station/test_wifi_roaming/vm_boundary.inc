
static unsigned submits;
static bool sdk_error;
static const char *esp_err_to_name(int err) {(void)err;return "injected";}
static esp_err_t esp32_mquickjs_wifi_roaming_execute(const esp32_mquickjs_wifi_btm_query_t *q,
    esp32_mquickjs_wifi_roaming_result_t *r) {
    *r=(esp32_mquickjs_wifi_roaming_result_t){.stage="complete",.entered=true,.rrm=true,.btm=true,.radio_generation=7};
    if(q){++submits;r->submitted=true;if(sdk_error){r->error=77;r->sdk_code=-55;}}
    return r->error;
}
