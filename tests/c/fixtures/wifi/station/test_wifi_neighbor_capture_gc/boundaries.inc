
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static bool request_locked;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!request_locked);request_locked=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(request_locked);request_locked=false;}while(0)
