
#include "cutils.h"
#include "esp32_mquickjs_wifi_csi_target.h"
#define CONFIG_ESP32_MQUICKJS_WIFI_CSI_POOL_CAPACITY 8
#define CONFIG_ESP32_MQUICKJS_WIFI_CSI_QUEUE_LEN 4
static bool esp32_mquickjs_wifi_csi_target_is_he(void){return false;}
static bool esp32_mquickjs_wifi_csi_target_supports_vht(void){return false;}
static bool esp32_mquickjs_wifi_csi_target_supports_lltf_bit_mode(void){return false;}
static JSValue property_with_fault(JSContext *ctx,JSValue value,const char *key){
    JSGCRef ref;JSValue *root=JS_PushGCRef(ctx,&ref);*root=value;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,key);
    JS_PopGCRef(ctx,&ref);return result;
}
#define JS_GetPropertyStr property_with_fault
static void *pool_calloc(size_t count,size_t size,void *opaque){(void)opaque;return heap_caps_calloc(count,size,1);}
static void *pool_malloc(size_t size,void *opaque){(void)opaque;return heap_caps_malloc(size,1);}
static void pool_free(void *ptr,void *opaque){(void)opaque;heap_caps_free(ptr);}
