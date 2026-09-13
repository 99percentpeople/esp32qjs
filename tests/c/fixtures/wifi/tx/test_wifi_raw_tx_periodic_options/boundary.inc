
#define ESP32_MQUICKJS_MEMORY_EXTERNAL 1
#undef esp32_mquickjs_memory_wireless_alloc
static void *esp32_mquickjs_memory_wireless_alloc(const char *tag,size_t length,int policy,int role) {
    assert(role==ESP32_MQUICKJS_MEMORY_BUDGET_TX);
    assert(!strcmp(tag,"wifi.raw-tx") && length>=24 && length<=1500 && policy==1);
    return heap_caps_malloc(length,1);
}
