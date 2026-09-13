
struct esp32_mquickjs_event_queue {unsigned references,sent;bool closed,full;esp32_mquickjs_wifi_nan_discovery_event_t last;};
static unsigned service_starts,service_cancels;
static bool service_cancel_pending,omit_binding,emit_early=true;
static int service_start_error;
static int64_t service_advance_us;
static uint8_t next_service_id=7;
bool esp32_mquickjs_event_queue_retain(esp32_mquickjs_event_queue_t*q){assert(q&&q->references);++q->references;return true;}
void esp32_mquickjs_event_queue_release(esp32_mquickjs_event_queue_t*q){assert(q&&q->references);--q->references;}
bool esp32_mquickjs_event_queue_is_closed(const esp32_mquickjs_event_queue_t*q){return q->closed;}
void esp32_mquickjs_event_queue_request_close(esp32_mquickjs_event_queue_t*q){q->closed=true;}
bool esp32_mquickjs_event_queue_try_send_from_callback(esp32_mquickjs_event_queue_t*q,const void *event){
 assert(!lock_depth);if(q->closed||q->full)return false;++q->sent;q->last=*(const esp32_mquickjs_wifi_nan_discovery_event_t*)event;return true;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_service_start(const esp32_mquickjs_wifi_radio_operation_t*t,
 const wifi_nan_publish_cfg_t*p,const wifi_nan_subscribe_cfg_t*u,uint8_t*id){
 assert(!lock_depth&&t->identity&&(!p!=!u)&&id&&!*id);++service_starts;
 if(service_start_error)return service_start_error;
 *id=next_service_id;
 esp32_mquickjs_wifi_nan_sdk_notice_t n={.kind=ESP32_MQUICKJS_NAN_SDK_SERVICE_BOUND,.service_id=*id,.context=p?2:1};
 if(!omit_binding)native_observer(begins,&n,native_opaque);
 if(emit_early){
  struct{wifi_event_nan_replied_t h;uint8_t tail[4];}e={.h={.publish_id=*id,.subscribe_id=3,.ssi_len=3},.tail={1,2,3,0}};
  memcpy(e.h.ssi,"abc",3);n=(esp32_mquickjs_wifi_nan_sdk_notice_t){.kind=ESP32_MQUICKJS_NAN_SDK_EVENT,.event_id=WIFI_EVENT_NAN_REPLIED,.data=&e,.size=sizeof(e)};
  native_observer(begins,&n,native_opaque);
 }
 clock_us+=service_advance_us;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_service_close(const esp32_mquickjs_wifi_radio_operation_t*t,uint8_t id,bool*cancelled){
 assert(!lock_depth&&t->identity&&id&&cancelled);if(!*cancelled){++service_cancels;*cancelled=true;}
 return service_cancel_pending?ESP_ERR_TIMEOUT:0;
}
