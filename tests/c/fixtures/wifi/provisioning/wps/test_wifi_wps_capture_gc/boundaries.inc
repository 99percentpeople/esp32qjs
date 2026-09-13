
static bool session_locked;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!session_locked);session_locked=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(session_locked);session_locked=false;}while(0)
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n){volatile uint8_t *b=p;while(n--)*b++=0;}
static const char *esp_err_to_name(int e){(void)e;return "injected";}
