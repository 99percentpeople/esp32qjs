
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1
#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_NO_MEM 4
typedef int esp_err_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static bool critical,allocation_fail;
static unsigned allocations,frees;
static struct {void *p;size_t bytes;} live[4];
#define portENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=false;}while(0)
#define MALLOC_CAP_8BIT 1
static void *heap_caps_calloc(size_t n,size_t size,int caps) {
    assert(!critical && n==1 && caps==MALLOC_CAP_8BIT);allocations++;
    if(allocation_fail)return NULL;
    void *p=calloc(n,size);assert(p);
    for(unsigned i=0;i<4;i++)if(!live[i].p){live[i].p=p;live[i].bytes=size;return p;}
    assert(0);return NULL;
}
static void heap_caps_free(void *p) {
    assert(!critical && p);frees++;
    for(unsigned i=0;i<4;i++)if(live[i].p==p) {
        /* Inspect actual bytes at production free boundary after secure-zero. */
        for(size_t j=0;j<live[i].bytes;j++)assert(!((uint8_t *)p)[j]);
        live[i].p=NULL;free(p);return;
    }
    assert(0);
}
