
static int wifi_radio_initialize(void) {assert(!"unexpected physical init");return 77;}
static int wifi_radio_policy_writer(void *opaque,esp32_mquickjs_wifi_policy_slot_t slot,bool requested) {
    (void)opaque;(void)slot;(void)requested;return sdk_step(true);
}
