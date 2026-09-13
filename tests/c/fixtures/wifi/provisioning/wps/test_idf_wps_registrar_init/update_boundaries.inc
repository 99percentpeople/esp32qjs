
struct wps_sm {struct wps_context *wps_ctx;struct wps_data *wps;};
static struct wps_sm sm;
static unsigned captures,null_captures,ie_updates;
static int capture_error;
static struct wps_sm *wps_sm_get(void){return &sm;}
static int hostapd_wps_config_ap(struct hostapd_data *hapd,struct wps_data *data){(void)hapd;captures++;if(!data){null_captures++;return ESP_ERR_INVALID_ARG;}assert(data==sm.wps);return capture_error;}
static void wps_registrar_update_ie(void *reg){(void)reg;ie_updates++;}
