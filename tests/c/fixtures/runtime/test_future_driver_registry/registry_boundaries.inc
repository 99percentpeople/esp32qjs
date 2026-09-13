
typedef struct{future_driver_chunk_t*drivers;size_t driver_count;bool shutting_down;}future_runtime_t;
typedef struct{future_runtime_t*state;}esp32_mquickjs_runtime_t;
static unsigned allocations,fail_allocation,live;
static future_runtime_t*future_runtime(esp32_mquickjs_runtime_t*r){return r?r->state:NULL;}
static bool JS_IsFunction(JSContext*ctx,JSValue value){(void)ctx;return value>0;}
static void JS_ThrowOutOfMemory(JSContext*ctx){*ctx=1;}
static void*future_runtime_resource_allocate(size_t n,size_t bytes,void*opaque){
 (void)opaque;if(++allocations==fail_allocation)return NULL;void*p=calloc(n,bytes);assert(p);++live;return p;}
static void future_runtime_resource_release(void*p,void*opaque){(void)opaque;
 for(unsigned i=0;i<root_count;++i)assert((uintptr_t)roots[i]<(uintptr_t)p||(uintptr_t)roots[i]>=(uintptr_t)p+sizeof(future_driver_chunk_t));
 assert(live);--live;free(p);}
#define ESP_LOGE(...) ((void)0)
