
#include <limits.h>
#define MALLOC_CAP_8BIT 1
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned roc_critical,roc_allocated;
static bool allocation_fail,worker_full;
static int64_t now_us;
static void (*worker_fn)(void *);
static void *worker_arg;
#define portENTER_CRITICAL(p) do {(void)(p);assert(!roc_critical && !locks && !critical);roc_critical=1;} while(0)
#define portEXIT_CRITICAL(p) do {(void)(p);assert(roc_critical);roc_critical=0;} while(0)
static void *heap_caps_calloc(size_t count,size_t size,int caps) {
    assert(!roc_critical && !locks && !critical && caps==1);if(allocation_fail)return NULL;
    void *p=calloc(count,size);assert(p);++roc_allocated;return p;
}
static void heap_caps_free(void *p) {assert(p && roc_allocated && !roc_critical && !critical && !locks);--roc_allocated;free(p);}
static int64_t esp_timer_get_time(void) {assert(!roc_critical);return now_us;}
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg) {
    assert(!roc_critical && !locks && !critical);if(worker_full)return false;
    assert(!worker_fn);worker_fn=fn;worker_arg=arg;return true;
}
