
int main(void) {
 struct wps_registrar reg={0};esp32qjs_wps_registrars=&reg;
 assert(!esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_pbc_timeout,&reg));
 struct pending old=last;uint64_t ticket=reg.esp32qjs_timer_tickets[0];int cancels=cancel_calls;
 register_error=-73;
 assert(esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_pbc_timeout,&reg)==-73);
 assert(reg.esp32qjs_timer_tickets[0]==ticket && cancel_calls==cancels);
 assert(esp32qjs_wps_registrar_last_ticket==ticket+1);
 old.fn(old.ctx,old.arg);assert(pbc_fired==1);
 esp32qjs_wps_registrar_timer_detach(&reg);
 assert(esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_pbc_timeout,&reg)==ESP_ERR_INVALID_STATE);
 return 0;
}
