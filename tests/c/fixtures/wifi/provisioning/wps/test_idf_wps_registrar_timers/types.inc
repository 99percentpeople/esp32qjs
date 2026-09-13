
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef void (*eloop_timeout_handler)(void*,void*);
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ETH_ALEN 6
#define WPS_UUID_LEN 16
#define WPS_PBC_WALK_TIME 120
#define os_memcpy memcpy
#define os_memset memset
#define MSG_DEBUG 0
#define wpa_printf(...) ((void)0)
#define wpa_hexdump(...) ((void)0)
#define wpa_hexdump_ascii_key(...) ((void)0)
struct dl_list{int unused;};
struct os_reltime{int sec;};
struct wps_registrar{struct wps_registrar *esp32qjs_next;uint64_t esp32qjs_timer_tickets[2];int pbc,selected_registrar,force_pbc_overlap;u8 p2p_dev_addr[6];void *wps;struct dl_list pins;};
struct pending{eloop_timeout_handler fn;void *ctx,*arg;};
static struct pending last;
static int register_error,cancel_calls,pbc_fired,pin_fired;
static int eloop_register_timeout(unsigned sec,unsigned usec,eloop_timeout_handler fn,void *ctx,void *arg){(void)sec;(void)usec;if(register_error)return register_error;last=(struct pending){fn,ctx,arg};return 0;}
static int eloop_cancel_timeout(eloop_timeout_handler fn,void *ctx,void *arg){(void)fn;(void)ctx;(void)arg;cancel_calls++;return 0;}
static void wps_registrar_pbc_timeout(void *ctx,void *arg){assert(ctx && !arg);pbc_fired++;}
static void wps_registrar_set_selected_timeout(void *ctx,void *arg){assert(ctx && !arg);pin_fired++;}
