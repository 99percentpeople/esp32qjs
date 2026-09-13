
static struct esp32qjs_wps_eapol_record *esp32qjs_wps_eapol_record(struct eapol_state_machine *sm){return (void*)sm;}
static void esp32qjs_wps_eapol_stop_record(struct eapol_state_machine *sm){esp32qjs_wps_eapol_record(sm)->stopped=true;}
