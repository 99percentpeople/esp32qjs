
struct esp32_mquickjs_wifi_smartconfig_decoder {esp32_mquickjs_wifi_smartconfig_decoder_status_t status;};
static struct esp32_mquickjs_wifi_smartconfig_decoder decoder;
static int peer_error=ESP_ERR_WIFI_NOT_CONNECT,start_error,close_error,channel_error;
static unsigned starts,stops,channel_writes,decoder_releases;
static bool mutate=true,construct_fail;
static uint8_t channel=6;
static wifi_second_chan_t secondary=WIFI_SECOND_CHAN_NONE;
static int fence_error;
static esp32_mquickjs_wifi_radio_operation_t posted_fence;
static esp_err_t esp_event_post(int base,int id,const void *data,size_t size,unsigned wait){
 assert(locks && !critical && base==ESP32QJS_WIFI_RADIO_CONTROL_EVENT && id==7 && size==sizeof(posted_fence) && !wait);
 if(!fence_error)memcpy(&posted_fence,data,size);return fence_error;
}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_validate_options(const esp32_mquickjs_wifi_smartconfig_decoder_options_t *o){assert(!locks && !critical);return o->channel_timeout_s>=15 ? ESP_OK : ESP_ERR_INVALID_ARG;}
static esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){assert(locks && !critical);memset(ap,0,sizeof(*ap));return peer_error;}
static esp_err_t wifi_radio_get_channel_locked(uint8_t *primary,wifi_second_chan_t *second,uint32_t *generation){assert(locks && !critical);*primary=channel;*second=secondary;*generation=1;return 0;}
static esp_err_t wifi_radio_validate_regulatory_channel(uint8_t primary){assert(locks && !critical && primary==6);return 0;}
static esp_err_t (esp_wifi_set_channel)(uint8_t primary,wifi_second_chan_t second){assert(locks && !critical);channel_writes++;if(!channel_error){channel=primary;secondary=second;}return channel_error;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_create(uint32_t generation,const esp32_mquickjs_wifi_smartconfig_decoder_options_t *options,esp32_mquickjs_wifi_smartconfig_decoder_t **out){assert(locks && !critical && !*out && options);if(construct_fail)return ESP_ERR_NO_MEM;memset(&decoder,0,sizeof(decoder));decoder.status.token=(esp32_mquickjs_wifi_smartconfig_token_t){111,generation};*out=&decoder;return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_start(esp32_mquickjs_wifi_smartconfig_decoder_t *d){assert(locks && !critical && d==&decoder);starts++;d->status.error=start_error;d->status.started=!start_error;d->status.mutation_attempted=mutate;if(mutate)channel=11;return start_error;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_status(esp32_mquickjs_wifi_smartconfig_decoder_t *d,esp32_mquickjs_wifi_smartconfig_decoder_status_t *out){assert(locks && !critical && d==&decoder);*out=d->status;return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_events_status(const esp32_mquickjs_wifi_smartconfig_token_t *t,esp32_mquickjs_wifi_smartconfig_events_status_t *s){assert(locks && !critical && t->identity==111);memset(s,0,sizeof(*s));s->token=*t;return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(esp32_mquickjs_wifi_smartconfig_decoder_t *d){assert(locks && !critical && d==&decoder);d->status.capture_stopped=true;d->status.started=false;return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_ack(esp32_mquickjs_wifi_smartconfig_decoder_t *d){assert(locks && !critical && d->status.capture_stopped);return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_credentials(esp32_mquickjs_wifi_smartconfig_decoder_t *d,esp32_mquickjs_wifi_smartconfig_credentials_t *out,bool commit){assert(locks && !critical && d->status.capture_stopped);if(!commit)memset(out,0,sizeof(*out));return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_close(esp32_mquickjs_wifi_smartconfig_decoder_t *d){assert(locks && !critical && d==&decoder);stops++;d->status.closing=true;if(close_error)return close_error;d->status.capture_stopped=d->status.retired=true;d->status.token.identity=0;return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_release(esp32_mquickjs_wifi_smartconfig_decoder_t **d){assert(locks && !critical && (*d)->status.retired);*d=NULL;decoder_releases++;return 0;}
