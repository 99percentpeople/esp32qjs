
int main(void){
    JSContext ctx=0;esp32_mquickjs_event_queue_runtime_t registry={0};
    esp32_mquickjs_runtime_t runtime={.event_queue_state=&registry};
    for(unsigned nth=1;nth<=10;++nth){
        fail_at=nth;allocation_calls=0;
        assert(esp32_mquickjs_event_queue_new_wireless("wifi",&ctx,&runtime,16,4,
            ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST,convert,NULL,NULL,NULL)==JS_EXCEPTION);
        empty();assert(!registry.head);
    }
    fail_at=0;
    bool *failures[]={&fail_js,&fail_mutex,&fail_queue};
    for(size_t i=0;i<3;++i){
        *failures[i]=true;
        assert(esp32_mquickjs_event_queue_new_wireless("wifi",&ctx,&runtime,16,4,
            ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST,convert,NULL,NULL,NULL)==JS_EXCEPTION);
        *failures[i]=false;empty();assert(!registry.head);
    }
    unsigned before=allocation_calls;
    assert(esp32_mquickjs_event_queue_new_wireless("wifi",&ctx,&runtime,SIZE_MAX,2,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,convert,NULL,NULL,NULL)==JS_EXCEPTION);
    assert(before==allocation_calls);
    assert(esp32_mquickjs_event_queue_new_wireless("wifi",&ctx,&runtime,16,4,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST,convert,NULL,NULL,NULL)==1);
    esp32_mquickjs_event_queue_t *q=object_queue;
    size_t initial=status().wireless.regions[0].reserved;
    void *probe=esp32_mquickjs_memory_wireless_alloc("fill",1,ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
    assert(probe);size_t overhead=status().wireless.regions[0].reserved-initial-1;
    esp32_mquickjs_memory_payload_free(probe);
    void *fill=esp32_mquickjs_memory_wireless_alloc("fill",3584-initial-overhead,ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL);assert(fill);
    future_slot_t slot={.memory_owner="wireless.future"};
    void *handle=future_control_allocate(&slot,1,16);assert(handle);
    esp32_mquickjs_memory_payload_free(handle);
    JSGCRef receiver={.val=1};esp32_mquickjs_future_driver_state_t *pending=NULL;
    assert(event_queue_future_prepare(&ctx,&receiver,0,NULL,&pending));
    assert(pending && roots==1 && q->native_retain_count==1);
    assert(status().wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL]>0);
    /* The real last native retain destroys a disposed queue only after receive storage is released. */
    q->dispose_requested=true;atomic_store(&q->closed,true);registry.head=NULL;
    event_queue_future_destroy(pending);
    assert(!roots && status().wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_QUEUE]==0);
    esp32_mquickjs_memory_payload_free(fill);empty();
    assert(esp32_mquickjs_event_queue_new(&ctx,&runtime,16,4,ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,convert,NULL,NULL,NULL)==1);
    assert(status().wireless.regions[0].reserved==0);registry.head=NULL;
    event_queue_destroy_native(object_queue);empty();assert(deletes>0);
    return 0;
}
