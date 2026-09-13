
#define PIN_EXPIRES 2
struct wps_uuid_pin{struct dl_list list;u8 uuid[16];int wildcard_uuid;u8 *pin;size_t pin_len;int flags;struct os_reltime expiration;u8 enrollee_addr[6];};
static int live,clears,linked,invalidated;
static struct wps_uuid_pin *published;
static void *os_zalloc(size_t n){void *p=calloc(1,n);assert(p);live++;return p;}
static void *os_memdup(const void *p,size_t n){void *q=os_zalloc(n);memcpy(q,p,n);return q;}
static void os_free(void *p){if(p){live--;free(p);}}
static void wps_free_pin(struct wps_uuid_pin *p){assert(p->pin_len==8);memset(p->pin,0,p->pin_len);clears++;os_free(p->pin);os_free(p);}
static void os_get_reltime(struct os_reltime *t){t->sec=1;}
static void wps_registrar_invalidate_unused(struct wps_registrar *r){(void)r;invalidated++;}
static void dl_list_add(struct dl_list *head,struct dl_list *entry){(void)head;published=(struct wps_uuid_pin*)entry;linked++;}
