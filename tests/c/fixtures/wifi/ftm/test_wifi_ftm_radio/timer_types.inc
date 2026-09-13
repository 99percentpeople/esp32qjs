
#include <stdatomic.h>
typedef void *esp_timer_handle_t;
typedef struct {void (*callback)(void *);void *arg;int dispatch_method;const char *name;} esp_timer_create_args_t;
#define ESP_TIMER_TASK 0
