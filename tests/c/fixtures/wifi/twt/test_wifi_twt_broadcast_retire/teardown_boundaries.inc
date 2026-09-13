
static unsigned pm_releases;
void pm_twt_wake_up(void){assert(!locked);}
void pm_twt_wake_done(void){assert(!locked);++pm_releases;}
void __real_he_twt_teardown_txcb(void *p){(void)p;assert(0);}
bool esp32_mquickjs_wifi_twt_tx_teardown_identity(void *p,uint32_t *id){(void)p;(void)id;assert(0);return false;}
bool esp32_mquickjs_wifi_twt_tx_broadcast_teardown_fields(void *p,bool completion,uint8_t *slot) {
    (void)p;(void)completion;(void)slot;assert(0);return false;
}
bool esp32_mquickjs_wifi_twt_sdk_teardown_tx_matches_native(uint32_t id,uintptr_t node,uint8_t flow) {
    (void)id;(void)node;(void)flow;assert(0);return false;
}
void esp32_mquickjs_wifi_twt_sdk_broadcast_teardown_complete_native(uintptr_t node,uint8_t slot,uint8_t status) {
    (void)node;(void)slot;(void)status;assert(0);
}
