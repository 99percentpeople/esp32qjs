
static esp32_mquickjs_wifi_policy_state_t state;
static esp32_mquickjs_wifi_policy_write_t result;
static esp32_mquickjs_wifi_policy_snapshot_t snapshot;
static unsigned calls,seen[4];static int failed_slot=-1;static bool values[4];
static int writer(void *p,esp32_mquickjs_wifi_policy_slot_t slot,bool value) {
    assert(p==&state);++calls;++seen[slot];values[slot]=value;
    assert(result.attempted && state.records[slot].uncertain && !state.records[slot].known);
    return (int)slot==failed_slot ? 77 : ESP_OK;
}
#define REPLAY(gen,after) esp32_mquickjs_wifi_policy_replay(&state,gen,&snapshot,after,&completed,writer,&state,&result)
int main(void) {
    uint8_t completed=0;
    assert(esp32_mquickjs_wifi_policy_capture(&state,7,&snapshot)==ESP_OK && !snapshot.mask);
    assert(REPLAY(7,false)==ESP_OK && !calls); /* Cold start without configured policies. */
    for(unsigned i=0;i<4;++i)assert(esp32_mquickjs_wifi_policy_apply(&state,7,i,(i&1)!=0,writer,&state,&result)==ESP_OK);
    assert(esp32_mquickjs_wifi_policy_capture(&state,8,&snapshot)==ESP_ERR_INVALID_STATE && !snapshot.mask);
    assert(esp32_mquickjs_wifi_policy_capture(&state,7,&snapshot)==ESP_OK && snapshot.mask==15 && snapshot.values==10);
    unsigned before=calls;assert(REPLAY(7,false)==ESP_ERR_INVALID_STATE && calls==before);
    esp32_mquickjs_wifi_policy_invalidate(&state);
    assert(REPLAY(8,true)==ESP_ERR_INVALID_STATE && calls==before); /* Never post before pre. */
    failed_slot=2;
    assert(REPLAY(8,false)==77 && completed==2 && snapshot.values==10 && state.records[2].uncertain);
    unsigned first=seen[1];failed_slot=-1;
    assert(REPLAY(8,false)==ESP_OK && completed==14 && seen[1]==first);
    assert(REPLAY(8,true)==ESP_OK && completed==15 && !values[0] && values[1] && !values[2] && values[3]);
    before=calls;assert(REPLAY(8,true)==ESP_OK && calls==before);
    esp32_mquickjs_wifi_policy_snapshot_t unchanged=snapshot;
    failed_slot=1;
    assert(esp32_mquickjs_wifi_policy_apply(&state,8,1,false,writer,&state,&result)==77);
    assert(esp32_mquickjs_wifi_policy_capture(&state,8,&snapshot)==ESP_ERR_INVALID_STATE && !snapshot.mask);
    snapshot=unchanged;esp32_mquickjs_wifi_policy_invalidate(&state);completed=0;failed_slot=-1;
    assert(REPLAY(9,false)==ESP_OK && REPLAY(9,true)==ESP_OK); /* Original frozen intent survives failed replay generation. */
    assert(!values[0] && values[1] && !values[2] && values[3]);
    state.revision=UINT32_MAX-3;
    assert(esp32_mquickjs_wifi_policy_capture(&state,9,&snapshot)==ESP_ERR_INVALID_STATE && !snapshot.mask);
    state.revision=UINT32_MAX-4;
    assert(esp32_mquickjs_wifi_policy_capture(&state,9,&snapshot)==ESP_OK);
    esp32_mquickjs_wifi_policy_invalidate(&state);completed=0;
    assert(REPLAY(10,false)==ESP_OK && REPLAY(10,true)==ESP_OK && state.revision==UINT32_MAX);
    assert(esp32_mquickjs_wifi_policy_capture(&state,10,&snapshot)==ESP_ERR_INVALID_STATE);
    return 0;
}
