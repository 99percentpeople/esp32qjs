
static esp32_mquickjs_event_queue_t *object_queue;
static bool fail_js,fail_mutex,fail_queue;
static unsigned roots,deletes;
#define JS_IsException(v) ((v)==JS_EXCEPTION)
static JSValue throw_error(JSContext *ctx,...) {(void)ctx;return JS_EXCEPTION;}
#define JS_ThrowInternalError throw_error
#define JS_ThrowRangeError throw_error
#define JS_ThrowTypeError throw_error
#define JS_ThrowOutOfMemory throw_error
static JSValue JS_NewObjectClassUser(JSContext *ctx,int id){(void)ctx;assert(id==1);return fail_js ? JS_EXCEPTION : 1;}
static void JS_SetOpaque(JSContext *ctx,JSValue object,void *p){(void)ctx;assert(object==1);object_queue=p;}
static JSValue *JS_AddGCRef(JSContext *ctx,JSGCRef *r){(void)ctx;++roots;return &r->val;}
static void JS_DeleteGCRef(JSContext *ctx,JSGCRef *r){(void)ctx;(void)r;assert(roots);--roots;}
static esp32_mquickjs_event_queue_t *event_queue_from_value(JSContext *ctx,JSValue object){(void)ctx;return object==1 ? object_queue : NULL;}
static bool esp32_mquickjs_value_to_bounded_u32(JSContext *ctx,JSValue v,uint32_t a,uint32_t b,uint32_t *out){(void)ctx;assert(v>=(intptr_t)a && v<=(intptr_t)b);*out=(uint32_t)v;return true;}
static int esp_timer_stop(esp_timer_handle_t p){(void)p;abort();}
static int esp_timer_delete(esp_timer_handle_t p){(void)p;abort();}
static SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *p){assert(!test_memory_lock_depth);if(fail_mutex)return NULL;p->magic=123;return p;}
static SemaphoreHandle_t xSemaphoreCreateMutex(void){StaticSemaphore_t *p=heap_caps_calloc(1,sizeof(*p),MALLOC_CAP_8BIT);if(p){p->dynamic=true;p->magic=123;}return p;}
static QueueHandle_t xQueueCreateStatic(uint32_t n,UBaseType_t size,uint8_t *bytes,StaticQueue_t *p){assert(!test_memory_lock_depth);if(fail_queue)return NULL;p->capacity=n;p->item_size=size;p->bytes=bytes;p->magic=123;return p;}
static QueueHandle_t xQueueCreate(uint32_t n,size_t size){StaticQueue_t *p=heap_caps_calloc(1,sizeof(*p)+n*size,MALLOC_CAP_8BIT);if(p){p->dynamic=true;p->magic=123;}return p;}
static void vQueueDelete(QueueHandle_t p){assert(!test_memory_lock_depth && p->magic==123);++deletes;if(p->dynamic)heap_caps_free(p);else {assert(status().wireless.regions[0].reserved>0);p->magic=0;}}
#define vSemaphoreDelete vQueueDelete
static JSValue convert(JSContext *ctx,const void *p,void *opaque){(void)ctx;(void)p;(void)opaque;return 0;}
