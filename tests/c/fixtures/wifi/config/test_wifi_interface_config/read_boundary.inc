
#define ESP32_MQUICKJS_WIFI_RADIO_STARTED 3
#define ESP_ERR_WIFI_NOT_INIT -7
static struct {
 int lock,driver_state;bool driver_owned,storage_configured,restart_required;
 const char *fault_stage,*cleanup_stage;struct{unsigned identity;}lifecycle,operation;
}s_radio;
static int depth,critical,reads,native_error;
static void wifi_radio_operation_lock(void){assert(!depth&&!critical);depth=1;}
static void wifi_radio_operation_unlock(void){assert(depth&&!critical);depth=0;}
static int esp_wifi_get_config(wifi_interface_t interface,wifi_config_t *out){
 assert(depth&&!critical);reads++;memset(out,0,sizeof(*out));
 if(interface==WIFI_IF_STA){memcpy(out->sta.ssid,"station",7);memcpy(out->sta.password,"native-secret",13);memcpy(out->sta.sae_h2e_identifier,"private",7);}
 else{out->ap.ssid[0]=255;out->ap.ssid[1]=0;out->ap.ssid[2]=65;out->ap.ssid_len=3;memcpy(out->ap.password,"ap-secret",9);}
 return native_error; /* Can fill the buffer before returning failure. */
}
