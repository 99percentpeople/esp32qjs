
 assert(ret==-73 && !reg.pbc && !reg.selected_registrar && reg.force_pbc_overlap==9);
 assert(!changed && !active_events && !authorized);
 register_error=0;assert(!wps_registrar_button_pushed(&reg,NULL));
 assert(reg.pbc && reg.selected_registrar && reg.esp32qjs_timer_tickets[0]);
 assert(changed==1 && active_events==1 && authorized==1);
