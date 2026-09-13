
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_INTERNAL 2
static bool allocation_fails,early_individual,accept_individual=true;
static unsigned individual_submits,teardowns,retire_steps;
static int individual_error,retirement_error;
static int32_t last_request=-1;
static esp32_mquickjs_wifi_twt_setup_result_t individual_results[8];
/* Only this SDK boundary value is injected, not an alternate Radio machine. */
typedef struct {int16_t individual_ids[8];uint32_t broadcast_id_bitmap;} esp32_mquickjs_wifi_twt_sdk_snapshot_t;
static esp32_mquickjs_wifi_twt_sdk_snapshot_t native_individual;
static void *heap_caps_calloc(size_t n,size_t size,unsigned caps) {
    assert(locks && !critical && caps==3);return allocation_fails?NULL:calloc(n,size);
}
void esp32_mquickjs_wifi_twt_setup_results_snapshot(esp32_mquickjs_wifi_twt_setup_results_snapshot_t *out) {
    assert(!critical);*out=(esp32_mquickjs_wifi_twt_setup_results_snapshot_t){.last_request_id=last_request};
}
void esp32_mquickjs_wifi_twt_setup_result_request_close(uint32_t id) {
    assert(!critical);
    for(unsigned i=0;i<8;++i)if(id && individual_results[i].identity==id)
        individual_results[i].flags|=ESP32_MQUICKJS_WIFI_TWT_SETUP_CLOSE_REQUESTED;
}
esp_err_t esp32_mquickjs_wifi_twt_setup_result_read(uint32_t id,esp32_mquickjs_wifi_twt_setup_result_t *out) {
    assert(!critical);
    for(unsigned i=0;i<8;++i)if(individual_results[i].identity==id){*out=individual_results[i];return 0;}
    return ESP_ERR_INVALID_STATE;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_individual_submit(const wifi_itwt_setup_config_t *cfg,
    esp32_mquickjs_wifi_twt_setup_dispatch_t *out) {
    assert(locks && !critical && cfg->twt_id>last_request);++individual_submits;
    if(early_individual) {
        esp32_mquickjs_wifi_twt_token_t tokens[8];unsigned count=esp32_mquickjs_wifi_radio_twt_individual_tokens(tokens,8,false);
        esp32_mquickjs_wifi_twt_individual_radio_state_t state;bool found=false;assert(count);
        for(unsigned i=0;i<count;++i) {
            assert(esp32_mquickjs_wifi_radio_twt_individual_status(&tokens[i],&state));
            if(state.dispatching && state.requested.twt_id==cfg->twt_id)found=true;
        }
        assert(found);
    }
    if(!accept_individual)return individual_error;
    last_request=cfg->twt_id;
    for(unsigned i=0;i<8;++i)if(!individual_results[i].identity){
        *out=(esp32_mquickjs_wifi_twt_setup_dispatch_t){.config=*cfg,.identity=(uint32_t)last_request+1,.sdk_error=individual_error};
        individual_results[i]=(esp32_mquickjs_wifi_twt_setup_result_t){.identity=out->identity,.request_id=last_request,
            .flags=ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTED};return individual_error;
    }
    assert(0);return ESP_ERR_NO_MEM;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_snapshot(esp32_mquickjs_wifi_twt_sdk_snapshot_t *out) {
    assert(locks && !critical);*out=native_individual;return 0;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_teardown(uint32_t id,uint8_t flow) {
    assert(locks && !critical);++teardowns;
    for(unsigned i=0;i<8;++i)if(individual_results[i].identity==id){
        assert(native_individual.individual_ids[flow]==individual_results[i].request_id);
        assert(!(individual_results[i].flags&ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED));
        individual_results[i].flags|=ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED;return 0;
    }
    assert(0);return ESP_ERR_INVALID_STATE;
}
esp_err_t esp32_mquickjs_wifi_twt_setup_retire_poll(esp32_mquickjs_wifi_twt_setup_retire_t *state,
    const esp32_mquickjs_wifi_twt_token_t *token,uint32_t id) {
    assert(locks && !critical && token->identity);++retire_steps;state->stage="injected-retirement";
    if(retirement_error)return retirement_error;
    for(unsigned i=0;i<8;++i)if(individual_results[i].identity==id)individual_results[i].identity=0;
    state->result_released=state->released=true;state->stage=NULL;return 0;
}
