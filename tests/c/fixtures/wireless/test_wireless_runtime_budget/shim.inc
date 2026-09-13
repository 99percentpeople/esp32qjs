
#include "esp32_mquickjs_future_runtime_resources.h"
#include "esp32_mquickjs_wifi_runtime_resources.h"
typedef unsigned UBaseType_t;
typedef struct {unsigned magic,capacity,item_size;uint8_t *bytes;} StaticQueue_t;
typedef StaticQueue_t StaticSemaphore_t,StaticEventGroup_t;
typedef StaticQueue_t *QueueHandle_t,*SemaphoreHandle_t,*EventGroupHandle_t;
typedef uint8_t StackType_t;
typedef struct {unsigned magic;StackType_t *stack;} StaticTask_t;
typedef StaticTask_t *TaskHandle_t;
typedef int esp32_mquickjs_runtime_t;
typedef struct {uint8_t slot;uint32_t generation;} esp32_mquickjs_future_token_t;
typedef void (*esp32_mquickjs_future_worker_fn_t)(void *);
static unsigned native_calls,native_fail,deleted,task_calls,task_fail,errors,submitted;
static struct {void *pointer;bool alive;} native_buffers[32];
static TaskHandle_t tasks[2];
#define CONFIG_ESP32_MQUICKJS_FUTURE_WORKER_POOL_SIZE 2
#define CONFIG_ESP32_MQUICKJS_FUTURE_WORKER_QUEUE_LEN 4
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define tskIDLE_PRIORITY 0
#define ESP_LOGE(tag,...) do{(void)(tag);++errors;}while(0)
static const char *TAG="fixture";
static bool native_create(void *pointer){
    if(++native_calls==native_fail)return false;
    for(unsigned i=0;i<32;++i)if(!native_buffers[i].pointer){
        native_buffers[i].pointer=pointer;native_buffers[i].alive=true;return true;
    }
    abort();
}
static void before_heap_free(void *pointer){
    if(!pointer)return;
    for(unsigned i=0;i<32;++i)if(native_buffers[i].pointer==pointer){
        assert(!native_buffers[i].alive);native_buffers[i].pointer=NULL;
    }
}
static QueueHandle_t xQueueCreateStatic(unsigned capacity,unsigned size,uint8_t *bytes,StaticQueue_t *control){
    assert(capacity && size && bytes==(uint8_t *)(control+1));
    if(!native_create(control))return NULL;
    *control=(StaticQueue_t){.magic=123,.capacity=capacity,.item_size=size,.bytes=bytes};return control;
}
static void vQueueDelete(QueueHandle_t queue){
    assert(queue->magic==123);queue->magic=0;++deleted;
    for(unsigned i=0;i<32;++i)if(native_buffers[i].pointer==queue){
        assert(native_buffers[i].alive);native_buffers[i].alive=false;return;
    }
    abort();
}
static SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *control){
    if(!native_create(control))return NULL;
    control->magic=123;return control;
}
#define vSemaphoreDelete vQueueDelete
static EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *control){return xSemaphoreCreateMutexStatic(control);}
#define vEventGroupDelete vQueueDelete
static bool esp32_mquickjs_future_wake(esp32_mquickjs_runtime_t *r,esp32_mquickjs_future_token_t t){(void)r;(void)t;abort();}
static int xQueueReceive(QueueHandle_t q,void *data,unsigned ticks){(void)q;(void)data;(void)ticks;abort();}
static int xQueueSend(QueueHandle_t q,const void *data,unsigned ticks){assert(q && data && ticks==0);++submitted;return pdTRUE;}
static TaskHandle_t xTaskCreateStatic(void (*function)(void *),const char *name,unsigned depth,void *arg,
    unsigned priority,StackType_t *stack,StaticTask_t *task){
    assert(function && !strcmp(name,"mqjs_io") && depth==4096 && arg==NULL && priority==2);
    if(++task_calls==task_fail)return NULL;
    assert(task->magic==0);task->magic=456;task->stack=stack;
    for(unsigned i=0;i<2;++i)if(!tasks[i]){tasks[i]=task;return task;}
    abort();
}
