
static esp32_mquickjs_wifi_interval_state_t state;
static esp32_mquickjs_wifi_interval_snapshot_t snapshot;
static esp32_mquickjs_wifi_interval_result_t result;
static unsigned calls;static int error;static uint16_t native_value;
static int writer(void *opaque,uint16_t value) {assert(opaque==&state);++calls;native_value=value;return error;}
#define WRITE(gen,value) esp32_mquickjs_wifi_interval_write(&state,gen,value,writer,&state,&result)
#define CAPTURE(gen) esp32_mquickjs_wifi_interval_capture(&state,gen,&snapshot)
#define REPLAY(gen) esp32_mquickjs_wifi_interval_replay(&state,gen,&snapshot,writer,&state,&result)
int main(void) {
    assert(CAPTURE(7)==ESP_ERR_INVALID_STATE && !snapshot.generation);
    assert(WRITE(7,0)==ESP_OK && CAPTURE(7)==ESP_OK && snapshot.value==0);
    unsigned before=calls;assert(REPLAY(7)==ESP_ERR_INVALID_STATE && !result.attempted && calls==before);
    assert(WRITE(7,300)==ESP_OK && CAPTURE(7)==ESP_OK && snapshot.value==300);
    esp32_mquickjs_wifi_interval_snapshot_t frozen=snapshot;
    esp32_mquickjs_wifi_interval_token_t token={0};
    assert(esp32_mquickjs_wifi_interval_acquire(&state,7,41,100,writer,&state,&token,&result)==ESP_OK);
    assert(CAPTURE(7)==ESP_ERR_INVALID_STATE && !snapshot.generation);
    assert(esp32_mquickjs_wifi_interval_release(&state,&token,writer,&state,&result)==ESP_OK);
    assert(CAPTURE(7)==ESP_OK);frozen=snapshot;
    assert(CAPTURE(8)==ESP_ERR_INVALID_STATE && !snapshot.generation);snapshot=frozen;
    assert(esp32_mquickjs_wifi_interval_invalidate(&state,7));
    before=calls;assert(REPLAY(8)==ESP_ERR_INVALID_STATE && calls==before); /* No accepted new baseline. */
    assert(WRITE(8,0)==ESP_OK);error=77;
    assert(REPLAY(8)==77 && result.attempted && state.uncertain && !state.known);
    assert(frozen.generation==snapshot.generation && frozen.revision==snapshot.revision && frozen.value==snapshot.value && native_value==300);
    before=calls;error=0;assert(REPLAY(8)==ESP_ERR_INVALID_STATE && calls==before);
    assert(CAPTURE(8)==ESP_ERR_INVALID_STATE && !snapshot.generation);snapshot=frozen;
    assert(esp32_mquickjs_wifi_interval_invalidate(&state,8) && WRITE(9,0)==ESP_OK);
    assert(REPLAY(9)==ESP_OK && state.known && !state.uncertain && state.value==300);
    state.revision=UINT32_MAX-1;assert(CAPTURE(9)==ESP_ERR_INVALID_STATE && !snapshot.generation);
    state.revision=UINT32_MAX-2;assert(CAPTURE(9)==ESP_OK);
    assert(esp32_mquickjs_wifi_interval_invalidate(&state,9) && WRITE(10,0)==ESP_OK);
    assert(REPLAY(10)==ESP_OK && state.revision==UINT32_MAX && native_value==300);
    assert(CAPTURE(10)==ESP_ERR_INVALID_STATE && !snapshot.generation);
    return 0;
}
