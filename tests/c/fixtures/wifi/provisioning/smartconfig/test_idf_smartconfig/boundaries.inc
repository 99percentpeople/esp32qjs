
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
typedef int esp_err_t;
typedef unsigned TickType_t;
typedef int portMUX_TYPE;
typedef enum {SC_TYPE_ESPTOUCH,SC_TYPE_AIRKISS,SC_TYPE_ESPTOUCH_AIRKISS,SC_TYPE_ESPTOUCH_V2} smartconfig_type_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_TIMEOUT 5
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdPASS 1
#define portTICK_PERIOD_MS 1
#define WIFI_IF_STA 0
#define SC_EVENT 6
#define SC_EVENT_SEND_ACK_DONE 7
#define LWIP_SOCKET_OFFSET 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
static int locked;
#define portENTER_CRITICAL(p) do { (void)(p);assert(!locked);locked=1; } while(0)
#define portEXIT_CRITICAL(p) do { (void)(p);assert(locked);locked=0; } while(0)
typedef struct { int unused; } esp_netif_t;
typedef struct { struct { uint32_t addr; } ip; } esp_netif_ip_info_t;
static esp_netif_t netif;
static uint8_t phone[4]={192,168,1,2};
static void (*scheduled)(void *);
static void *scheduled_arg;
static int creates,deletes,posts,sends,closes,delay_count,stop_on_delay;
static int queue_error,create_error,mac_error,socket_error,send_error;
static bool immediate,missing_netif,allocation_error;
static struct { void *p;size_t size; } allocations[4];
esp_err_t sc_send_ack_start(smartconfig_type_t,uint8_t,uint8_t *);
void sc_send_ack_stop(void);
uint64_t esp32qjs_smartconfig_ack_status(bool *,bool *,bool *,esp_err_t *,esp_err_t *,int *);
static void *test_calloc(size_t n,size_t size) {
    assert(!locked);if(allocation_error)return NULL;
    void *p=calloc(n,size);assert(p);
    for(int i=0;i<4;i++)if(!allocations[i].p){allocations[i].p=p;allocations[i].size=n*size;return p;}
    assert(0);return NULL;
}
static void test_free(void *p) {
    assert(!locked);
    for(int i=0;i<4;i++)if(allocations[i].p==p){
        for(size_t j=0;j<allocations[i].size;j++)assert(((uint8_t *)p)[j]==0);
        allocations[i].p=NULL;free(p);return;
    }
    assert(0);
}
void esp32_mquickjs_wireless_secure_zero(void *p,size_t size) {
    volatile uint8_t *v=p;while(size--)*v++=0;
}
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,int priority,void *out) {
    assert(!locked && !scheduled && name && stack==2048 && priority==2 && !out);creates++;
    if(create_error)return 0;
    if(immediate)fn(arg);else {scheduled=fn;scheduled_arg=arg;}
    return pdPASS;
}
static void vTaskDelete(void *task) {assert(!locked && !task);deletes++;}
static void vTaskDelay(unsigned ticks) {
    assert(!locked && ticks);delay_count++;
    if(stop_on_delay && delay_count==stop_on_delay){
        sc_send_ack_stop();
        /* The old task still owns its socket/argument, even after STOP. */
        assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,2,phone)==ESP_ERR_INVALID_STATE);
    }
}
static esp_err_t esp_wifi_get_mac(int interface,uint8_t *mac) {
    assert(!locked && interface==0);memset(mac,0x12,6);return mac_error;
}
static esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) {
    assert(!locked && !strcmp(key,"WIFI_STA_DEF"));return missing_netif?NULL:&netif;
}
static esp_err_t esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *ip) {
    assert(!locked && n==&netif);ip->ip.addr=1;return ESP_OK;
}
static int test_socket(int af,int type,int protocol) {
    assert(!locked && af==AF_INET && type==SOCK_DGRAM && !protocol);
    if(socket_error){errno=ENOBUFS;return -1;}return 9;
}
static int test_setsockopt(int fd,int level,int option,const void *value,socklen_t len) {
    assert(!locked && fd==9 && level==SOL_SOCKET && option && value && len);return 0;
}
static int test_bind(int fd,const struct sockaddr *addr,socklen_t size) {
    assert(!locked && fd==9 && addr && size);return 0;
}
static int test_recvfrom(int fd,void *data,size_t size,int flags,struct sockaddr *addr,socklen_t *len) {
    assert(!locked && fd==9 && size==1 && !flags && len);*(char *)data=0;
    ((struct sockaddr_in *)addr)->sin_addr.s_addr=2;return 1;
}
static int test_sendto(int fd,const void *data,size_t len,int flags,const struct sockaddr *addr,socklen_t size) {
    assert(!locked && fd==9 && data && (len==7 || len==11) && flags==MSG_DONTWAIT && addr && size);
    sends++;if(send_error){errno=EIO;return -1;}return (int)len;
}
static int test_close(int fd) {assert(!locked && fd==9);closes++;return 0;}
static esp_err_t esp_event_post(int base,int event,const void *data,size_t len,unsigned wait) {
    assert(!locked && base==SC_EVENT && event==SC_EVENT_SEND_ACK_DONE && !data && !len && !wait);
    bool busy,completed;
    assert(esp32qjs_smartconfig_ack_status(&busy,NULL,&completed,NULL,NULL,NULL));
    assert(busy && completed);posts++;return queue_error;
}
#define calloc test_calloc
#define free test_free
#define socket test_socket
#define setsockopt test_setsockopt
#define bind test_bind
#define recvfrom test_recvfrom
#define sendto test_sendto
#define close test_close
