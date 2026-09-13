
static esp32_mquickjs_wifi_tx_rate_state_t state={.next_identity=1};
static esp32_mquickjs_wifi_tx_rate_snapshot_t snapshot;
static esp32_mquickjs_wifi_tx_rate_write_t output;
static unsigned calls,per_interface[2];static int fail_interface=-1;
static wifi_tx_rate_config_t observed[2];
static int writer(void *opaque,wifi_interface_t iface,const wifi_tx_rate_config_t *config) {
    assert(opaque==&state);++calls;++per_interface[iface];observed[iface]=*config;
    return fail_interface==(int)iface ? 77 : ESP_OK;
}
#define CAPTURE(gen) esp32_mquickjs_wifi_tx_rate_restart_capture(&state,gen,&snapshot)
#define REPLAY(gen) esp32_mquickjs_wifi_tx_rate_replay(&state,gen,&snapshot,&completed,writer,&state,&output)
int main(void) {
    uint8_t completed=0;
    assert(CAPTURE(7)==ESP_OK && snapshot.mask==0 && REPLAY(8)==ESP_OK && !calls);
    wifi_tx_rate_config_t wanted={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M};
    for(unsigned i=0;i<2;++i)assert(esp32_mquickjs_wifi_tx_rate_apply(&state,7,i,&wanted,writer,&state,&output)==ESP_OK);
    assert(CAPTURE(8)==ESP_ERR_INVALID_STATE && !snapshot.generation);
    assert(CAPTURE(7)==ESP_OK && snapshot.mask==3);
    esp32_mquickjs_wifi_tx_rate_snapshot_t frozen=snapshot;
    unsigned before=calls;assert(REPLAY(7)==ESP_ERR_INVALID_STATE && calls==before);
    esp32_mquickjs_wifi_tx_rate_invalidate(&state);fail_interface=1;
    assert(REPLAY(8)==77 && completed==1 && state.records[0].known && state.records[1].uncertain);
    assert(snapshot.generation==7 && snapshot.configs[0].rate==WIFI_PHY_RATE_6M && snapshot.configs[1].rate==WIFI_PHY_RATE_6M);
    unsigned station_writes=per_interface[0];fail_interface=-1;
    assert(REPLAY(8)==ESP_OK && completed==3 && per_interface[0]==station_writes);
    before=calls;assert(REPLAY(8)==ESP_OK && calls==before);
    esp32_mquickjs_wifi_tx_rate_invalidate(&state);completed=0;
    assert(REPLAY(9)==ESP_OK && completed==3 && state.records[0].generation==9 && state.records[1].generation==9);
    state.records[0].uncertain=true;
    assert(CAPTURE(9)==ESP_ERR_INVALID_STATE && !snapshot.generation);state.records[0].uncertain=false;
    state.next_identity=UINT32_MAX;assert(CAPTURE(9)==ESP_ERR_INVALID_STATE && !snapshot.generation);
    state.next_identity=UINT32_MAX-1;assert(CAPTURE(9)==ESP_OK);
    esp32_mquickjs_wifi_tx_rate_invalidate(&state);completed=0;
    assert(REPLAY(10)==ESP_OK && completed==3 && state.next_identity==0);
    assert(CAPTURE(10)==ESP_ERR_INVALID_STATE && !snapshot.generation);
    snapshot=frozen;before=calls;completed=0;
    assert(!esp32_mquickjs_wifi_tx_rate_replay_capacity(&state,&snapshot));
    assert(REPLAY(11)==ESP_ERR_INVALID_STATE && calls==before && !output.attempted);
    return 0;
}
