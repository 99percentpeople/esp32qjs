
static int starts,transactions,prepare_error;
static int esp32_mquickjs_wifi_ensure_started(void) { starts++;return prepare_error; }
static bool esp32_mquickjs_wifi_radio_accept_station_config(const wifi_config_t *a,const wifi_config_t *b) {
    return !memcmp(a,b,sizeof(*a));
}
static int wifi_configure_selected_interfaces(
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    wifi_config_t *station,bool (*accept)(const wifi_config_t *,const wifi_config_t *),
    wifi_config_t *ap,void *accept_ap,const void *controls,const void *start_controls,void *execution) {
    assert(!selection->mode_set && !selection->storage_set && !selection->start_only);
    assert(selection->station_set && !selection->access_point_set);
    assert(selection->start_set && selection->start && selection->allow_disconnect);
    assert(station && accept==esp32_mquickjs_wifi_radio_accept_station_config);
    assert(!ap && !accept_ap && !controls && !start_controls && !execution);
    assert(!station->sta.pmf_cfg.capable && !station->sta.pmf_cfg.required);
    transactions++;return prepare_error;
}
