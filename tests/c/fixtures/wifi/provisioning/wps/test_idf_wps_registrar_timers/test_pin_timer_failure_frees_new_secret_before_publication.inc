
int main(void) {
 struct wps_registrar reg={.selected_registrar=1};esp32qjs_wps_registrars=&reg;
 const u8 pin[8]={'1','2','3','4','5','6','7','0'};
 assert(!esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_set_selected_timeout,&reg));
 uint64_t old=reg.esp32qjs_timer_tickets[1];
 register_error=-73;
 assert(wps_registrar_add_pin(&reg,NULL,NULL,pin,8,120)==-73);
 assert(!live && clears==1 && !linked && !invalidated && !changed && !authorized);
 assert(reg.esp32qjs_timer_tickets[1]==old && reg.selected_registrar==1);
 register_error=0;
 assert(!wps_registrar_add_pin(&reg,NULL,NULL,pin,8,120));
 assert(live==2 && linked==1 && invalidated==1 && changed==1 && authorized==1);
 assert(reg.esp32qjs_timer_tickets[1]>old);
 wps_free_pin(published);assert(!live && clears==2);
 esp32qjs_wps_registrar_timer_detach(&reg);
 return 0;
}
