
struct mock_queue {
 wps_watch_source_t *source;wps_watch_event_t last;
 unsigned refs,sends,drops;bool closed,disposed,full;
};
static struct mock_queue q;
static void wps_watch_closed(void *);
static void wps_watch_destroyed(void *);
static bool esp32_mquickjs_event_queue_retain(esp32_mquickjs_event_queue_t *queue){
 assert(locks==1);if(queue->disposed)return false;queue->refs++;return true;
}
static void esp32_mquickjs_event_queue_release(esp32_mquickjs_event_queue_t *queue){assert(!locks && queue->refs);queue->refs--;}
static bool esp32_mquickjs_event_queue_close(esp32_mquickjs_event_queue_t *queue){
 assert(!locks);if(queue->closed)return false;queue->closed=true;wps_watch_closed(queue->source);return true;
}
static void esp32_mquickjs_event_queue_request_close(esp32_mquickjs_event_queue_t *queue){(void)esp32_mquickjs_event_queue_close(queue);}
static bool esp32_mquickjs_event_queue_try_send_from_callback(esp32_mquickjs_event_queue_t *queue,const void *event){
 assert(!locks && queue->refs);queue->sends++;
 if(queue->closed || queue->full){queue->drops++;return false;}
 queue->last=*(const wps_watch_event_t *)event;return true;
}
