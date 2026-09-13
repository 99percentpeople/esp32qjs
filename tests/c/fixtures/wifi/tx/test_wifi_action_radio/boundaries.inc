
#include <stdlib.h>
#define ESP_ERR_TIMEOUT 99
#define ESP_ERR_WIFI_NOT_CONNECT 100
#define ESP32QJS_WIFI_RADIO_CONTROL_EVENT 7
#define WIFI_EVENT_ACTION_TX_STATUS 19
#define WIFI_EVENT_ROC_DONE 20
#define RADIO_EVENTS_IDLE 0
typedef int esp_event_base_t;
static unsigned action_calls,cancel_calls,fence_calls,posts;
static int sdk_error,cancel_error,post_error,fence_error;
static int quiescent_error=ESP_ERR_TIMEOUT;
static unsigned quiescent_calls;
static bool quiescent_event_race;
static bool connected,early,late_in_post,queued;
static uint8_t sdk_id=255;
static wifi_radio_action_fence_t marker;
static void wifi_radio_action_event(int32_t id,const void *data);
static void wifi_radio_lifecycle_fence(void *,esp_event_base_t,int32_t,void *);
static int esp32_mquickjs_wifi_action_receive(uint8_t *a,uint8_t *b,size_t c,uint8_t d){(void)a;(void)b;(void)c;(void)d;return 0;}
static int esp_wifi_get_mode(wifi_mode_t *mode){assert(locks && !critical);*mode=s_radio.effective_mode;return 0;}
static int wifi_radio_validate_regulatory_channel(uint8_t channel){assert(locks && !critical);return channel<=11?0:ESP_ERR_INVALID_ARG;}
static int wifi_radio_get_channel_locked(uint8_t *channel,wifi_second_chan_t *secondary,uint32_t *generation){
    assert(locks && !critical);*channel=6;*secondary=WIFI_SECOND_CHAN_NONE;*generation=s_radio.generation;return 0;
}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){(void)ap;assert(locks && !critical);return connected?0:ESP_ERR_WIFI_NOT_CONNECT;}
static void action_event(unsigned status){
    wifi_event_action_tx_status_t event={.ifx=WIFI_IF_STA,.context=(uint32_t)(uintptr_t)esp32_mquickjs_wifi_action_receive,
        .channel=6,.op_id=sdk_id,.status=status};wifi_radio_action_event(WIFI_EVENT_ACTION_TX_STATUS,&event);
}
/* Undef only to declare the injected SDK functions; reapply real production
 * aliases below. The fixture's existing invalidation counter remains active. */
#undef esp_wifi_action_tx_req
#undef esp_wifi_remain_on_channel
static int esp_wifi_action_tx_req(wifi_action_tx_req_t *request){
    assert(locks && !critical && s_radio.operation.identity && s_action.lease.acquired);
    assert(s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ACTION]==1);
    if(request->type==WIFI_OFFCHAN_TX_CANCEL){++cancel_calls;assert(request->op_id==sdk_id);return cancel_error;}
    ++action_calls;request->op_id=sdk_id;if(early){action_event(0);action_event(2);}return sdk_error;
}
static int esp_wifi_remain_on_channel(wifi_roc_req_t *request){
    assert(locks && !critical && s_radio.operation.identity);
    if(request->type==WIFI_ROC_CANCEL){++cancel_calls;return cancel_error;}
    ++action_calls;request->op_id=sdk_id;return sdk_error;
}
#define esp_wifi_action_tx_req(...) WIFI_RADIO_MUTATION(esp_wifi_action_tx_req(__VA_ARGS__))
#define esp_wifi_remain_on_channel(...) WIFI_RADIO_MUTATION(esp_wifi_remain_on_channel(__VA_ARGS__))
static int esp32_mquickjs_wifi_action_sdk_fence(void){assert(locks && !critical);++fence_calls;return fence_error;}
static int esp32_mquickjs_wifi_action_sdk_quiescent(void) {
    assert(locks && !critical);++quiescent_calls;
    if(quiescent_event_race){quiescent_event_race=false;action_event(99);}
    return quiescent_error;
}
static int esp_event_post(int base,int id,const void *data,size_t length,unsigned ticks){
    assert(locks && !critical && base==7 && id==2 && length==sizeof(marker) && !ticks);++posts;
    if(late_in_post){late_in_post=false;action_event(2);}
    if(post_error)return post_error;marker=*(const wifi_radio_action_fence_t *)data;queued=true;return 0;
}
