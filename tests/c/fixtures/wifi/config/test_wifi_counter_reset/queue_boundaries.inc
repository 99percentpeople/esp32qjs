
static _Thread_local unsigned critical;
#define portENTER_CRITICAL(p) do {assert(!critical);assert(!pthread_mutex_lock(p));++critical;} while(0)
#define portEXIT_CRITICAL(p) do {assert(critical==1);--critical;assert(!pthread_mutex_unlock(p));} while(0)
#define portENTER_CRITICAL_ISR portENTER_CRITICAL
#define portEXIT_CRITICAL_ISR portEXIT_CRITICAL
typedef struct {unsigned count;int values[3];} queue_boundary_t;
typedef queue_boundary_t *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 99
static unsigned freed,wakes;
static pthread_mutex_t race_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t race_changed=PTHREAD_COND_INITIALIZER;
static bool race,submitted;
static int xSemaphoreTake(void *p,int wait){assert(p && wait==portMAX_DELAY && !critical);return pdTRUE;}
static int xSemaphoreGive(void *p){assert(p&&!critical);return pdTRUE;}
static int xQueueSend(QueueHandle_t q,const void *event,int wait){
    assert(critical==1&&!wait);if(q->count==3)return pdFALSE;
    q->values[q->count++]=*(const int *)event;
    if(race){pthread_mutex_lock(&race_lock);submitted=true;pthread_cond_signal(&race_changed);pthread_mutex_unlock(&race_lock);}
    return pdTRUE;
}
static int xQueueSendFromISR(QueueHandle_t q,const void *event,int *woken){*woken=0;return xQueueSend(q,event,0);}
static int xQueueReceive(QueueHandle_t q,void *event,int wait){
    assert(critical==1&&!wait);if(!q->count)return pdFALSE;
    *(int *)event=q->values[0];--q->count;memmove(q->values,q->values+1,q->count*sizeof(int));return pdTRUE;
}
static unsigned uxQueueMessagesWaitingFromISR(QueueHandle_t q){assert(critical==1);return q->count;}
static void event_queue_wake_receiver(esp32_mquickjs_event_queue_t *q){assert(q&&!critical);++wakes;}
static bool esp32_mquickjs_future_wake_from_isr(void *r,esp32_mquickjs_future_token_t t,int *woken){(void)r;(void)t;assert(!critical);*woken=0;++wakes;return true;}
static void drop(void *event,void *opaque){(void)opaque;assert(!critical&&*(int *)event>0);++freed;}
