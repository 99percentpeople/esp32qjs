"""Production SDK timer/stop/destroy regression; run in the Wi-Fi phase suite."""
import os
import pathlib
import sys
import unittest

from test_wireless_control_regression import compile_run, function

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_netif_timer import patch_source


class IDFNetifTimer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            raise unittest.SkipTest('Set IDF_PATH to the reviewed SDK for production timer regression')
        cls.source = (pathlib.Path(sdk) / 'components/esp_netif/lwip/esp_netif_lwip.c').read_bytes()
        cls.patched = patch_source(cls.source)

    def test_source_drift_and_double_patch_are_rejected(self):
        for content in [self.source + b'\n', self.patched]:
            with self.subTest(length=len(content)), self.assertRaises(ValueError):
                patch_source(content)

    def test_original_address_reuse_and_patched_stop_destroy(self):
        for patched, content in [(False, self.source), (True, self.patched)]:
            for enabled in [False, True]:
                with self.subTest(patched=patched, timer_enabled=enabled):
                    source = content.decode()
                    body = 'static void esp_netif_ip_lost_timer(void *arg);\n'
                    if patched:
                        body += function(source, 'esp32qjs_netif_cancel_ip_lost_timer')
                    for name in ['esp_netif_destroy_api', 'esp_netif_stop_api',
                                 'esp_netif_ip_lost_timer', 'esp_netif_start_ip_lost_timer']:
                        body += function(source, name)
                    compile_run(self, f'#define PATCHED {int(patched)}\n'
                                f'#define CONFIG_ESP_NETIF_LOST_IP_TIMER_ENABLE {int(enabled)}\n'
                                + BOUNDARIES + body + MAIN)


BOUNDARIES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define CONFIG_LWIP_IPV4 1
#define CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL 120
#define CONFIG_PPP_SUPPORT 0
#define ESP_DHCPS 0
#define ESP_OK 0
#define ESP_ERR_NOT_SUPPORTED -1
#define ESP_ERR_ESP_NETIF_IF_NOT_READY -2
#define ESP_NETIF_STOPPED 1
#define ESP_NETIF_LOST_IP 2
#define ESP_NETIF_DHCP_CLIENT 1
#define ESP_NETIF_DHCP_SERVER 2
#define ESP_NETIF_DHCP_STARTED 1
#define ESP_NETIF_DHCP_INIT 0
#define ESP_NETIF_IS_POINT2POINT_TYPE(...) 0
#define ESP_LOGV(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define LOG_NETIF_DISABLED_AND_DO(message, action) do { action; } while(0)
#define IP_EVENT 7
#define IP4_ADDR_ANY4 (&zero_ip)
#define ip_2_ip4(p) (p)
#define ip4_addr_cmp(a,b) ((a)->addr==(b)->addr)
typedef int esp_err_t;
typedef struct { unsigned addr; } ip4_addr_t;
static const ip4_addr_t zero_ip;
struct netif { bool up;ip4_addr_t ip_addr; };
typedef struct { ip4_addr_t ip; } esp_netif_ip_info_t;
typedef struct {
    void *if_key,*if_desc,*hostname,*dhcps;
    esp_netif_ip_info_t *ip_info,*ip_info_old;
    struct netif *lwip_netif;
    bool timer_running;
    unsigned flags;
    int dhcpc_status,dhcps_status,lost_ip_event;
} esp_netif_t;
typedef struct { esp_netif_t *esp_netif; } esp_netif_api_msg_t;
typedef struct { esp_netif_t *esp_netif; } ip_event_got_ip_t;
static struct { void *callback_fn; } netif_callback;
static esp_netif_t object;
static struct netif stack;
static esp_netif_ip_info_t info,old_info;
static bool listed,freed;
static int emitted,foreign_fired;
static struct { void (*fn)(void *);void *arg; } timers[4];
static int foreign_arg;
static void foreign_timer(void *arg) { assert(arg==&foreign_arg);foreign_fired++; }
static void sys_timeout(unsigned ms,void (*fn)(void *),void *arg) {
    assert(ms==120000);for(int i=0;i<4;i++)if(!timers[i].fn){timers[i].fn=fn;timers[i].arg=arg;return;}assert(0);
}
static void sys_untimeout(void (*fn)(void *),void *arg) {
    for(int i=0;i<4;i++)if(timers[i].fn==fn && timers[i].arg==arg){timers[i].fn=NULL;return;}
}
static int pending(void) { int count=0;for(int i=0;i<4;i++)count+=timers[i].fn!=NULL;return count; }
static void advance(void) {
    for(int i=0;i<4;i++)if(timers[i].fn){void (*fn)(void *)=timers[i].fn;void *arg=timers[i].arg;timers[i].fn=NULL;fn(arg);}
}
static void create_at_same_address(void) {
    memset(&object,0,sizeof(object));memset(&stack,0,sizeof(stack));
    object.lwip_netif=&stack;object.ip_info=&info;object.ip_info_old=&old_info;
    object.lost_ip_event=3;listed=true;freed=false;
}
static esp_netif_t *esp_netif_is_active(esp_netif_t *n) { return listed && n==&object?n:NULL; }
static void esp_netif_remove_from_list_unsafe(esp_netif_t *n) { assert(n==&object && listed);listed=false; }
static int esp_netif_get_nr_of_ifs(void) { return listed; }
static void netif_remove_ext_callback(void *p) { assert(p==&netif_callback); }
static void test_free(void *p) { if(p==&object){assert(!listed);freed=true;} }
#define free(p) test_free(p)
static void esp_netif_lwip_remove(esp_netif_t *n) { assert(n==&object);n->lwip_netif->up=false; }
static void esp_netif_destroy_related(esp_netif_t *n) { assert(n==&object); }
static void esp_netif_update_default_netif(esp_netif_t *n,int status) { assert(n==&object && status); }
static bool netif_is_up(struct netif *n) { return n->up; }
static void netif_set_down(struct netif *n) { n->up=false; }
static void dhcp_release(struct netif *n) { assert(n); }
static void dhcp_stop(struct netif *n) { assert(n); }
static void dhcp_cleanup(struct netif *n) { assert(n); }
static int esp_netif_reset_ip_info(esp_netif_t *n) { assert(n);return 0; }
static int esp_event_post(int base,int id,const void *data,size_t length,int wait) {
    const ip_event_got_ip_t *event=data;
    assert(base==IP_EVENT && id==3 && length==sizeof(*event) && !wait);
    assert(event->esp_netif==&object && listed && !freed);emitted++;return 0;
}
'''

MAIN = r'''
int main(void) {
    esp_netif_api_msg_t msg={.esp_netif=&object};
    for(int mode=0;mode<3;mode++) {
        create_at_same_address();memset(timers,0,sizeof(timers));emitted=foreign_fired=0;
        stack.up=mode==2;
        assert(esp_netif_start_ip_lost_timer(&object,false)==ESP_OK);
        sys_timeout(120000,foreign_timer,&foreign_arg);
        assert(pending()==1+CONFIG_ESP_NETIF_LOST_IP_TIMER_ENABLE);
        if(mode==0) {
            /* Direct SDK destroy, followed by allocator reuse of the exact address. */
            assert(esp_netif_destroy_api(&msg)==ESP_OK && freed);
            create_at_same_address();
        } else {
            /* STOP must also retire the timer before the already-down early return. */
            assert(esp_netif_stop_api(&msg)==(mode==1?ESP_ERR_ESP_NETIF_IF_NOT_READY:ESP_OK));
            assert(!stack.up);
            if(PATCHED)assert(!object.timer_running);
        }
        assert(pending()==1+(!PATCHED && CONFIG_ESP_NETIF_LOST_IP_TIMER_ENABLE));
        advance();
        /* Original SDK emits a stale event against the new object; patched code
         * removes the exact timer without cancelling an unrelated callback. */
        assert(emitted==(!PATCHED && CONFIG_ESP_NETIF_LOST_IP_TIMER_ENABLE));
        assert(foreign_fired==1 && pending()==0);
        assert(esp_netif_destroy_api(&msg)==ESP_OK && freed && !pending());
    }
    return 0;
}
'''
