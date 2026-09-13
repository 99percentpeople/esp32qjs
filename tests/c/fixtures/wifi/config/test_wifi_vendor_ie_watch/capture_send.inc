
static vendor_watch_event_t last;
static bool esp32_mquickjs_event_queue_try_send_from_callback(esp32_mquickjs_event_queue_t *queue,const void *event) {
    assert(locked);if(queue->full)return false;++queue->count;last=*(const vendor_watch_event_t *)event;return true;
}
