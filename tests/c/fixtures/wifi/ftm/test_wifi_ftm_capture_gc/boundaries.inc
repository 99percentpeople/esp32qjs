
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static bool session_locked;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!session_locked);session_locked=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(session_locked);session_locked=false;}while(0)
static const char *esp_err_to_name(int error){(void)error;return "injected";}
