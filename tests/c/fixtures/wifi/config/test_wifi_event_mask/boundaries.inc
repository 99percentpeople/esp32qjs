
#define WIFI_EVENT_MASK_AP_PROBEREQRECVED (1U << 0)
static uint32_t native_mask;
static bool corrupt_mask_readback,corrupt_mask_rollback;
static unsigned unsafe_writes;
static int esp_wifi_get_event_mask(uint32_t *out) {
    *out=native_mask;
    if(corrupt_mask_readback && !strcmp(result.stage,"event-mask-readback") &&
       (!result.rollback_attempted || corrupt_mask_rollback))*out^=1U;
    return sdk_step(false);
}
static int esp_wifi_set_event_mask(uint32_t mask) {
    if(mask & ~WIFI_EVENT_MASK_AP_PROBEREQRECVED)++unsafe_writes;
    native_mask=mask;return sdk_step(true);
}
