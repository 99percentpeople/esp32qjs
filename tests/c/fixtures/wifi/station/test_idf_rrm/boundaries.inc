
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
#define wpa_printf(...) ((void)0)
#define wpa_dbg(...) ((void)0)
#define wpa_hexdump(...) ((void)0)
#define wpa_msg(...) ((void)0)
#define os_memcpy memcpy
#define WLAN_RRM_CAPS_NEIGHBOR_REPORT 2
#define WLAN_ACTION_RADIO_MEASUREMENT 5
#define WLAN_RRM_NEIGHBOR_REPORT_REQUEST 4
#define WLAN_EID_SSID 0
#define WLAN_EID_MEASURE_REQUEST 38
#define MEASURE_REQUEST_LCI_LEN 8
#define MEASURE_REQUEST_CIVIC_LEN 8
#define MEASURE_TYPE_LCI 8
#define MEASURE_TYPE_LOCATION_CIVIC 11
#define LOCATION_SUBJECT_REMOTE 1
#define LCI_REQ_SUBELEM_MAX_AGE 4
#define RRM_NEIGHBOR_REPORT_TIMEOUT 1
typedef void (*callback_t)(void *, const u8 *, size_t);
struct rrm_data {
    unsigned rrm_used:1;
    callback_t notify_neighbor_rep;
    void *neighbor_rep_cb_ctx;
    u8 next_neighbor_rep_token;
};
struct wpa_supplicant { struct rrm_data rrm; u8 rrm_ie[1]; void *current_bss; };
struct wpa_ssid_value { u8 ssid[32]; size_t ssid_len; };
struct wifi_ssid { u8 ssid[32]; u8 len; };
static struct wpa_supplicant g_wpa_supp;
static struct wifi_ssid profile = {{'a',0,'b'},3};
static struct wifi_ssid *esp_wifi_sta_get_prof_ssid_internal(void) { return &profile; }
static void wpas_clear_beacon_rep_data(struct wpa_supplicant *s) { (void)s; }
struct wpabuf { u8 data[128]; size_t length, capacity; };
static struct wpabuf packet;
static int fail_alloc, tx_rc, timer_rc, allocations, transmissions, observations;
static bool queue_full;
static u8 sent_token;
static struct wpabuf *wpabuf_alloc(size_t n) {
    if (fail_alloc) return NULL;
    assert(!allocations && n<=sizeof(packet.data)); allocations++;
    packet=(struct wpabuf){.capacity=n}; return &packet;
}
static void wpabuf_free(struct wpabuf *p) { assert(p==&packet && allocations==1); allocations--; }
static void wpabuf_put_u8(struct wpabuf *p,unsigned v) {
    assert(p->length<p->capacity); p->data[p->length++]=(u8)v;
}
static void wpabuf_put_le16(struct wpabuf *p,unsigned v) { wpabuf_put_u8(p,v);wpabuf_put_u8(p,v>>8); }
static void wpabuf_put_data(struct wpabuf *p,const void *data,size_t n) {
    assert(n<=p->capacity-p->length);memcpy(p->data+p->length,data,n);p->length+=n;
}
static const void *wpabuf_head(struct wpabuf *p) { return p->data; }
static size_t wpabuf_len(struct wpabuf *p) { return p->length; }
static int wpa_drv_send_action(struct wpa_supplicant *s,int a,int b,const void *data,size_t n,int c) {
    (void)s;(void)a;(void)b;(void)c;assert(n>=3);sent_token=((const u8 *)data)[2];
    transmissions++;return tx_rc;
}
static struct { void (*fn)(void *,void *);void *data,*ctx; } timer;
static int eloop_register_timeout(int sec,int usec,void (*fn)(void *,void *),void *data,void *ctx) {
    assert(sec==1 && !usec && !timer.fn);
    if (timer_rc) return timer_rc;
    timer.fn=fn;timer.data=data;timer.ctx=ctx;return 0;
}
static int eloop_cancel_timeout(void (*fn)(void *,void *),void *data,void *ctx) {
    if(timer.fn==fn && timer.data==data && timer.ctx==ctx){timer.fn=NULL;return 1;}return 0;
}
static void fire_timer(void) {
    assert(timer.fn);void (*fn)(void *,void *)=timer.fn;timer.fn=NULL;fn(timer.data,timer.ctx);
}
void neighbor_report_recvd_cb(void *ctx,const u8 *data,size_t n) {
    assert(!ctx);assert((data!=NULL)==(n!=0));if(!queue_full)observations++;
}
static int identities[2],callbacks,null_callbacks;
static void record(void *ctx,const u8 *data,size_t n) {
    assert(ctx==&identities[0]);assert(g_wpa_supp.rrm.notify_neighbor_rep==record);
    /* SDK owns callback storage throughout this call, including timeout. */
    assert(g_wpa_supp.rrm.neighbor_rep_cb_ctx==ctx);
    callbacks++;if(!data){assert(!n);null_callbacks++;}
}
