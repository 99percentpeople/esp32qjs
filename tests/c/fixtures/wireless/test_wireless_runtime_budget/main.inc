
static void work(void *p){(void)p;abort();}
int main(void){
    esp32_mquickjs_memory_init();
    for(unsigned nth=1;nth<=10;++nth){
        esp32_mquickjs_wifi_runtime_resources_t r;
        allocation_calls=0;fail_at=nth;
        assert(!esp32_mquickjs_wifi_runtime_resources_init(&r,&s_wifi_runtime_resource_ops,2,16,2,16,4,16));
        empty();
    }
    fail_at=0;
    for(unsigned nth=1;nth<=5;++nth){
        esp32_mquickjs_wifi_runtime_resources_t r;native_calls=0;native_fail=nth;
        assert(!esp32_mquickjs_wifi_runtime_resources_init(&r,&s_wifi_runtime_resource_ops,2,16,2,16,4,16));
        empty();
    }
    native_fail=0;unsigned previous=allocation_calls;
    assert(!esp32_mquickjs_memory_queue_create("wifi",SIZE_MAX,16,false));
    assert(!esp32_mquickjs_memory_queue_create("wifi",2,SIZE_MAX,false));
    assert(!esp32_mquickjs_memory_queue_create("wifi",0,16,false));
    assert(previous==allocation_calls);
    esp32_mquickjs_wifi_runtime_resources_t wifi;
    assert(esp32_mquickjs_wifi_runtime_resources_init(&wifi,&s_wifi_runtime_resource_ops,2,16,2,16,4,16));
    assert(status().wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL]>0);
    esp32_mquickjs_wifi_runtime_resources_deinit(&wifi,&s_wifi_runtime_resource_ops);empty();
    void *probe=esp32_mquickjs_memory_wireless_alloc("fill",1,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL);assert(probe);
    size_t overhead=status().wireless.regions[0].reserved-1;
    esp32_mquickjs_memory_payload_free(probe);
    void *fill=esp32_mquickjs_memory_wireless_alloc("fill",32768-512-overhead,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL);assert(fill);
    assert(!esp32_mquickjs_memory_queue_create("wifi",1,1,false));
    QueueHandle_t control=esp32_mquickjs_memory_queue_create("wifi",1,1,true);assert(control);
    esp32_mquickjs_memory_queue_delete(control);
    esp32_mquickjs_memory_payload_free(fill);empty();
    for(unsigned nth=1;nth<=8;++nth){
        esp32_mquickjs_future_runtime_resources_t r;allocation_calls=0;fail_at=nth;
        assert(!esp32_mquickjs_future_runtime_resources_init(&r,&s_future_runtime_resource_ops,64,4,32,4,8));
        empty();
    }
    fail_at=0;
    for(unsigned nth=1;nth<=4;++nth){
        allocation_calls=0;fail_at=nth;
        assert(!future_init_worker_pool());
        assert(!s_future_workers_created && !s_future_worker_queue && !s_future_worker_storage);
        empty();
    }
    fail_at=0;task_calls=0;task_fail=1;
    assert(!future_init_worker_pool());empty();
    task_calls=0;task_fail=2;
    assert(!future_init_worker_pool());
    assert(s_future_workers_created==1 && tasks[0] && !tasks[1]);
    assert(!esp32_mquickjs_submit_background_worker(work,NULL) && !submitted);
    size_t retained=status().wireless.regions[0].reserved;
    assert(status().wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_STACK]>=8192);
    QueueHandle_t queue=s_future_worker_queue;TaskHandle_t first=tasks[0];
    previous=allocation_calls;task_fail=0;
    assert(future_init_worker_pool());
    assert(task_calls==3 && allocation_calls==previous && tasks[0]==first && tasks[1]);
    assert(s_future_worker_queue==queue && s_future_workers_created==2);
    assert(esp32_mquickjs_submit_background_worker(work,NULL) && submitted==1);
    for(unsigned cycle=0;cycle<3;++cycle){
        esp32_mquickjs_future_runtime_resources_t r;
        assert(future_init_worker_pool() && task_calls==3);
        assert(esp32_mquickjs_future_runtime_resources_init(&r,&s_future_runtime_resource_ops,64,4,32,4,8));
        esp32_mquickjs_future_runtime_resources_deinit(&r,&s_future_runtime_resource_ops);
        esp32_mquickjs_memory_release_generation();
        assert(status().wireless.regions[0].reserved==retained && tasks[0]==first);
    }
    /* A created pool has boot lifetime. Do not manufacture task retirement in
     * this fixture or release live static stacks to obtain a zero ledger. */
    return 0;
}
