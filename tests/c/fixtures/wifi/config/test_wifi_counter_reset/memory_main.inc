
int main(void){
    psram=true;esp32_mquickjs_memory_init();
    void *pool=esp32_mquickjs_memory_wireless_alloc("wifi",64,ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL);assert(pool);
    assert(esp32_mquickjs_memory_wireless_retire(pool));
    void *control=esp32_mquickjs_memory_wireless_alloc("wifi",32,ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);assert(control);
    assert(!esp32_mquickjs_memory_wireless_alloc("wifi",100000,ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL));
    esp32_mquickjs_memory_status_t before=status();assert(before.allocation_failures&&before.wireless.rejected);
    assert(before.wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_RETIRED_POOL]==64);
    assert(before.wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL]==32);
    unsigned calls=allocation_calls;
    esp32_mquickjs_memory_reset_counters();esp32_mquickjs_memory_status_t after=status();
    assert(calls==allocation_calls&&!after.allocation_failures&&!after.wireless.rejected);
    assert(before.managed_internal_bytes==after.managed_internal_bytes&&before.managed_psram_bytes==after.managed_psram_bytes);
    assert(before.allocation_count==after.allocation_count&&!memcmp(before.allocations,after.allocations,sizeof(before.allocations)));
    for(unsigned i=0;i<2;i++){
        assert(after.wireless.regions[i].high_water==after.wireless.regions[i].reserved);
        assert(before.wireless.regions[i].reserved==after.wireless.regions[i].reserved);
        assert(!memcmp(before.wireless.regions[i].roles,after.wireless.regions[i].roles,sizeof(before.wireless.regions[i].roles)));
    }
    esp32_mquickjs_memory_payload_free(pool);esp32_mquickjs_memory_payload_free(control);empty();
}
