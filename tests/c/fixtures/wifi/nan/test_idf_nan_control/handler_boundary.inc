
#include <assert.h>
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define IP_EVENT 1
#define IP_EVENT_GOT_IP6 2
static bool s_app_default_handlers_set=true;
static int calls,error=-71;
static void nan_app_action_got_ipv6(void){}
static int esp_event_handler_unregister(int base,int id,void(*handler)(void)){
    assert(base==1&&id==2&&handler==nan_app_action_got_ipv6);++calls;return error;
}
