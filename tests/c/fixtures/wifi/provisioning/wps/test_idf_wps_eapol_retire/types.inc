
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static int critical,context,destroyed,wake_error;
static bool native=true,schedule_interleave;
static uint32_t identity=7;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=0;}while(0)
struct eapol_state_machine {int unused;};
struct sta_info {struct eapol_state_machine *eapol_sm;};
static void (*queued)(void*,void*);
static void *queued_arg;
static bool current_task_is_wifi_task(void){return native;}
static uint32_t esp32qjs_wps_ap_result_identity(const void *p){return native && p==&context?identity:0;}
static void *hostapd_get_hapd_data(void){return &context;}
static int eloop_register_timeout(unsigned,unsigned,void(*)(void*,void*),void*,void*);
static int eloop_cancel_timeout(void(*)(void*,void*),void*,void*);
