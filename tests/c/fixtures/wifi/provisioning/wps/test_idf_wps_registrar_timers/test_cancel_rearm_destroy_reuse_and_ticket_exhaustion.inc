
int main(void) {
 struct wps_registrar a={0},b={0};a.esp32qjs_next=&b;esp32qjs_wps_registrars=&a;
 assert(!esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_pbc_timeout,&a));
 struct pending old=last;uint64_t first=a.esp32qjs_timer_tickets[0];
 assert(last.ctx!=&a && last.ctx!=&b && last.arg!=&a && last.arg!=&b);
 assert(!esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_pbc_timeout,&a));
 struct pending current=last;
 assert(a.esp32qjs_timer_tickets[0]>first);
 old.fn(old.ctx,old.arg);assert(!pbc_fired);
 current.fn(current.ctx,current.arg);assert(pbc_fired==1 && !a.esp32qjs_timer_tickets[0]);
 current.fn(current.ctx,current.arg);assert(pbc_fired==1);
 assert(!esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_set_selected_timeout,&b));
 struct pending other=last;
 assert(!esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_pbc_timeout,&a));old=last;
 esp32qjs_wps_registrar_timer_detach(&a);
 assert(esp32qjs_wps_registrars==&b);
 memset(&a,0,sizeof(a));a.esp32qjs_next=&b;esp32qjs_wps_registrars=&a; /* Same allocator address, new native instance. */
 assert(!esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_pbc_timeout,&a));current=last;
 old.fn(old.ctx,old.arg);assert(pbc_fired==1);
 other.fn(other.ctx,other.arg);assert(pin_fired==1);
 current.fn(current.ctx,current.arg);assert(pbc_fired==2);
 esp32qjs_wps_registrar_last_ticket=UINT32_MAX;
 assert(!esp32qjs_wps_registrar_timer_arm(1,0,wps_registrar_pbc_timeout,&a));
 assert((uintptr_t)last.ctx==1 && (uintptr_t)last.arg==0);last.fn(last.ctx,last.arg);assert(pbc_fired==3);
 esp32qjs_wps_registrar_last_ticket=UINT64_MAX-1;
 assert(!esp32qjs_wps_registrar_timer_arm(1,0,wps_registrar_pbc_timeout,&a));old=last;
 assert(a.esp32qjs_timer_tickets[0]==UINT64_MAX);
 assert(esp32qjs_wps_registrar_timer_arm(1,0,wps_registrar_pbc_timeout,&a)==ESP_ERR_NO_MEM);
 assert(a.esp32qjs_timer_tickets[0]==UINT64_MAX);old.fn(old.ctx,old.arg);assert(pbc_fired==4);
 esp32qjs_wps_registrar_timer_detach(&a);esp32qjs_wps_registrar_timer_detach(&b);
 assert(!esp32qjs_wps_registrars && esp32qjs_wps_registrar_last_ticket==UINT64_MAX);
 return 0;
}
