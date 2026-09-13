
static void *consumer(void *opaque){
    pthread_mutex_lock(&race_lock);while(!submitted)pthread_cond_wait(&race_changed,&race_lock);pthread_mutex_unlock(&race_lock);
    int event;assert(esp32_mquickjs_event_queue_try_receive(opaque,&event)&&event==7);return NULL;
}
int main(void){
    queue_boundary_t native={0};int scratch=0,drain=0;
    esp32_mquickjs_event_queue_t q={.resources={.events=&native,.send_lock=&native,.overflow_scratch=&scratch,.drain_scratch=&drain},.capacity=3,.drop=drop};
    assert(!pthread_mutex_init(&q.lock,NULL));
    esp32_mquickjs_event_queue_runtime_t registry={.head=&q};
    esp32_mquickjs_runtime_t runtime={.event_queue_state=&registry};q.runtime=&runtime;
    int event=1,woken=0;
    assert(esp32_mquickjs_event_queue_send(&q,&event));event=2;
    assert(esp32_mquickjs_event_queue_try_send_from_callback(&q,&event));event=3;
    assert(esp32_mquickjs_event_queue_send_from_isr(&q,&event,&woken));
    esp32_mquickjs_event_queue_stats_t stats;assert(esp32_mquickjs_event_queue_get_stats(&q,&stats));
    assert(stats.queued==3&&stats.high_water==3&&!stats.dropped);
    event=4;assert(!esp32_mquickjs_event_queue_send(&q,&event)&&q.dropped==1);
    q.overflow=ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST;
    assert(esp32_mquickjs_event_queue_send(&q,&event)&&freed==1&&q.dropped==2);
    int value;assert(esp32_mquickjs_event_queue_try_receive(&q,&value)&&value==2);
    q.receiver_registered=true;q.receiver=(esp32_mquickjs_future_token_t){2,19};q.native_retain_count=4;
    esp32_mquickjs_reset_event_queue_counters(&runtime);
    assert(q.high_water==2&&!q.dropped&&native.count==2&&q.receiver_registered&&q.receiver.generation==19&&q.native_retain_count==4);
    q.dropped=UINT32_MAX;event=5;assert(esp32_mquickjs_event_queue_send(&q,&event));event=6;
    assert(esp32_mquickjs_event_queue_send(&q,&event)&&q.dropped==UINT32_MAX);
    esp32_mquickjs_event_queue_status_t total;assert(esp32_mquickjs_get_event_queue_status(&runtime,&total));
    assert(total.dropped==UINT32_MAX&&total.high_water==3&&total.queued==3&&total.open==1);
    assert(esp32_mquickjs_event_queue_discard_all(&q)==3&&native.count==0);
    esp32_mquickjs_reset_event_queue_counters(&runtime);assert(!q.high_water);
    race=true;pthread_t reader;assert(!pthread_create(&reader,NULL,consumer,&q));event=7;
    assert(esp32_mquickjs_event_queue_send(&q,&event));assert(!pthread_join(reader,NULL));
    assert(esp32_mquickjs_event_queue_get_stats(&q,&stats)&&stats.queued==0&&stats.high_water==1);
    atomic_store(&q.closed,true);unsigned prior=wakes;assert(!esp32_mquickjs_event_queue_send(&q,&event)&&wakes==prior);
    esp32_mquickjs_reset_event_queue_counters(&runtime);assert(atomic_load(&q.closed)&&q.receiver_registered&&q.native_retain_count==4);
    assert(!critical);pthread_mutex_destroy(&q.lock);
}
