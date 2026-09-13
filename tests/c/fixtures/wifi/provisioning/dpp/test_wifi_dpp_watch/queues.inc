
struct mock_queue {
 dpp_watch_source_t *source;dpp_watch_event_t last;
 unsigned refs,sends,drops;bool closed,disposed,full;
};
static struct mock_queue q;
static void dpp_watch_closed(void *);
static void dpp_watch_destroyed(void *);
static bool esp32_mquickjs_event_queue_retain(esp32_mquickjs_event_queue_t *queue){
 assert(locks==1);if(queue->disposed)return false;queue->refs++;return true;
}
static void esp32_mquickjs_event_queue_release(esp32_mquickjs_event_queue_t *queue){assert(!locks && queue->refs);queue->refs--;}
static bool esp32_mquickjs_event_queue_close(esp32_mquickjs_event_queue_t *queue){
 assert(!locks);if(queue->closed)return false;queue->closed=true;dpp_watch_closed(queue->source);return true;
}
static void esp32_mquickjs_event_queue_request_close(esp32_mquickjs_event_queue_t *queue){(void)esp32_mquickjs_event_queue_close(queue);}
static bool esp32_mquickjs_event_queue_try_send_from_callback(esp32_mquickjs_event_queue_t *queue,const void *event){
 assert(!locks && queue->refs);queue->sends++;
 if(queue->closed || queue->full){queue->drops++;return false;}
 queue->last=*(const dpp_watch_event_t *)event;return true;
}
