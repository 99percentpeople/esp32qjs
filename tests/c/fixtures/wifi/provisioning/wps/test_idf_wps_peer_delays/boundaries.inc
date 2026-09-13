
static struct esp32qjs_wps_peer_record a;
static void setup(void){h.sta_list=&a.sta;a.sta.lock=&available;}
static void ap_free_sta(struct hostapd_data *p,struct sta_info *s){assert(!table_lock && p==&h && s==&a.sta);h.sta_list=s->next;removed++;}
static void esp32qjs_wps_ap_peer_release(void *p,void *s){assert(p==&h && s==&a.sta);if(a.sta.remove_pending)ap_free_sta(p,s);else available=true;}
