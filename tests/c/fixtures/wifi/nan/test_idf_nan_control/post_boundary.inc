
static unsigned notified,posted,wait_ticks;
#define WIFI_EVENT 1
#define OSI_FUNCS_TIME_BLOCKING UINT32_MAX
static int native_post(int base,int32_t id,void *data,size_t size,unsigned wait){
    assert(base==1&&id==7&&data&&size==4);posted++;wait_ticks=wait;return ESP_ERR_NO_MEM;
}
static struct{int(*_event_post)(int,int32_t,void*,size_t,unsigned);}g_wifi_osi_funcs={native_post};
