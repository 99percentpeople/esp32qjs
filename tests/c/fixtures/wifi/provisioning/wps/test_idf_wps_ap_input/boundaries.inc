
static void setup(void){peer.lock=&available;h.sta_list=&peer;h.wps=&h;}
static struct wpabuf *new_ie(void){struct wpabuf *p=calloc(1,sizeof(*p));assert(p);live++;return p;}
static struct wpabuf *ieee802_11_vendor_ie_concat(const u8 *data,size_t len,unsigned oui){assert(data && len==sizeof(ie) && oui==WPS_DEV_OUI_WFA);concat_calls++;return oom?NULL:new_ie();}
