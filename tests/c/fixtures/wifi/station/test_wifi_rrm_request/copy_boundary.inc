
static bool close_during_copy;
static esp32_mquickjs_wifi_rrm_request_t *closing;
static void *interleaved_copy(void *destination,const void *source,size_t length) {
    if(close_during_copy && source==payload) {
        close_during_copy=false;
        esp32_mquickjs_wifi_rrm_counts_t counts;esp32_mquickjs_wifi_rrm_counts(&counts);
        assert(counts.callback_busy && !request_lock);
        esp32_mquickjs_wifi_rrm_close(closing);
        esp32_mquickjs_wifi_rrm_status_t status;assert(esp32_mquickjs_wifi_rrm_status(closing,&status));
        assert(status.closed && status.retained_bytes==16);
    }
    return memcpy(destination,source,length);
}
#define memcpy interleaved_copy
