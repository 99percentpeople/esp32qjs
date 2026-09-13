
static int changed,active_events,authorized;
static int wps_registrar_pbc_overlap(struct wps_registrar *r,void *a,void *b){(void)r;(void)a;(void)b;return 0;}
static void wps_pbc_overlap_event(void *r){(void)r;assert(0);}
static void wps_registrar_add_authorized_mac(struct wps_registrar *r,const u8 *addr){(void)r;(void)addr;authorized++;}
static void wps_registrar_selected_registrar_changed(struct wps_registrar *r,int pw){(void)r;(void)pw;changed++;}
static void wps_pbc_active_event(void *r){(void)r;active_events++;}
