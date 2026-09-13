
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define WIFI_FUTURE_DISCONNECT 2
#define WIFI_FUTURE_SCAN 0
typedef struct { bool started, completed; unsigned generation; int kind; struct { struct { unsigned char password[64]; } sta; } connect_config; } esp32_mquickjs_future_driver_state_t;
static void esp32_mquickjs_wifi_clear_connect_future(void) {}
static int esp32_mquickjs_wifi_cancel_scan(unsigned generation) { (void)generation;return 0; }
static int wifi_future_cancel(esp32_mquickjs_future_driver_state_t *s) { (void)s; return 0; }
static void esp32_mquickjs_wireless_secure_zero(void *p, size_t n) { volatile unsigned char *v=p; while(n--) *v++=0; }
static void heap_caps_free(esp32_mquickjs_future_driver_state_t *s) {
    for (unsigned i=0; i<sizeof(s->connect_config.sta.password); i++) assert(s->connect_config.sta.password[i]==0);
    free(s);
}
