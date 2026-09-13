
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t u8;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_ARG -3
#define ESP_ERR_NOT_SUPPORTED -4
#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE 1
#define os_memcpy memcpy
#define os_strlen strlen
#define os_strnlen strnlen
#define os_strcmp strcmp
#define os_strcat strcat
#define os_snprintf snprintf
#define os_bzero(p,n) memset(p,0,n)
static struct {void *pointer;size_t size;} blocks[128];
static unsigned active,dirty_frees,timers;
static bool fail_next;
static void *os_zalloc(size_t size) {
    if(fail_next){fail_next=false;return NULL;}
    void *p=calloc(1,size);assert(p);
    for(unsigned i=0;i<128;i++)if(!blocks[i].pointer){blocks[i].pointer=p;blocks[i].size=size;active++;return p;}
    assert(0);return NULL;
}
static void os_free(void *p) {
    if(!p)return;
    for(unsigned i=0;i<128;i++)if(blocks[i].pointer==p) {
        bool dirty=false;for(size_t j=0;j<blocks[i].size;j++)if(((u8 *)p)[j])dirty=true;
        if(dirty)dirty_frees++;
        blocks[i].pointer=NULL;active--;free(p);return;
    }
    assert(!"unknown allocation/double free");
}
static char *os_strdup(const char *s) {size_t n=strlen(s)+1;char *p=os_zalloc(n);if(p)memcpy(p,s,n);return p;}
static void config_changed_handler(void *a,void *b) {(void)a;(void)b;}
static int eloop_register_timeout(unsigned sec,unsigned usec,void (*callback)(void *,void *),void *a,void *b) {
    assert(!sec && !usec && callback==config_changed_handler && !a && !b);timers++;return 0;
}
/* SDK storage shape at the actual deinit boundary; no replacement state machine. */
struct eap_peer_config {
    u8 *anonymous_identity,*identity,*password,*new_password;
    size_t anonymous_identity_len,identity_len,password_len,new_password_len;
    void *eap_methods;
};
struct eap_sm {struct eap_peer_config config;};
static void *config_methods;
