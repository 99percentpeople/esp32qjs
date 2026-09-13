
static esp32_mquickjs_wifi_ftm_offset_state_t state;
static unsigned calls;
static int failure;
static int16_t native;
static int writer(void *opaque, int16_t value) {
    assert(opaque == &state && state.uncertain && !state.known);
    assert(state.requested_cm == value); ++calls; native = value;
    return failure; /* Mutation may precede error. */
}
int main(void) {
    bool attempted,completed=false;
    esp32_mquickjs_wifi_ftm_offset_snapshot_t snapshot;
#define APPLY(gen,value) esp32_mquickjs_wifi_ftm_offset_apply(&state,gen,value,writer,&state,&attempted)
#define CAPTURE(gen) esp32_mquickjs_wifi_ftm_offset_capture(&state,gen,&snapshot)
#define REPLAY(gen) esp32_mquickjs_wifi_ftm_offset_replay(&state,gen,&snapshot,&completed,writer,&state)
    assert(CAPTURE(1)==ESP_OK && !snapshot.configured && !calls);
    assert(REPLAY(2)==ESP_OK && completed && !calls);completed=false;
    assert(APPLY(0,0)==ESP_ERR_INVALID_ARG && !attempted && !calls);
    assert(APPLY(1,INT16_MIN)==ESP_OK && native==INT16_MIN && state.known && state.accepted_revision==1);
    failure=77;assert(APPLY(1,INT16_MAX)==77 && native==INT16_MAX && state.uncertain && !state.known);
    assert(state.accepted_cm==INT16_MIN && state.accepted_revision==1 && state.revision==2);
    assert(CAPTURE(1)==ESP_ERR_INVALID_STATE && !snapshot.configured);
    assert(APPLY(2,0)==ESP_ERR_INVALID_STATE && calls==2);
    failure=0;assert(APPLY(1,0)==ESP_OK && state.known && state.accepted_cm==0);
    assert(CAPTURE(1)==ESP_OK && snapshot.configured && snapshot.centimeters==0);
    assert(REPLAY(1)==ESP_ERR_INVALID_STATE && !completed);
    esp32_mquickjs_wifi_ftm_offset_invalidate(&state);
    assert(!state.known && !state.uncertain && state.configured && state.accepted_revision==3);
    assert(REPLAY(2)==ESP_OK && completed && state.generation==2 && native==0);
    unsigned before=calls;assert(REPLAY(2)==ESP_OK && calls==before);
    esp32_mquickjs_wifi_ftm_offset_invalidate(&state);
    assert(REPLAY(2)==ESP_ERR_INVALID_STATE && calls==before);
    completed=false;failure=88;assert(REPLAY(3)==88 && !completed && state.uncertain);
    failure=0;assert(REPLAY(3)==ESP_OK && completed && state.known);
    assert(CAPTURE(2)==ESP_ERR_INVALID_STATE);
    state.revision=UINT32_MAX-1;
    assert(APPLY(3,INT16_MAX)==ESP_OK && state.revision==UINT32_MAX && native==INT16_MAX);
    assert(CAPTURE(3)==ESP_ERR_INVALID_STATE);
    before=calls;assert(APPLY(3,1)==ESP_ERR_INVALID_STATE && !attempted && calls==before);
    esp32_mquickjs_wifi_ftm_offset_invalidate(&state);
    assert(APPLY(4,0)==ESP_ERR_INVALID_STATE && calls==before);
    return 0;
}
