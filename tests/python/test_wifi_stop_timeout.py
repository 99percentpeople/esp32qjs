"""Production stop capture and shared budget/netif scheduling. Phase run deferred."""
import tempfile
import unittest
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import ROOT, CORE, build, extract, run
import test_wifi_netif_retirement as netif_fixture

WAIT = ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_wait.c'
CONFIG = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_config.c'


def wait_source():
    return '\n'.join(line for line in WAIT.read_text().splitlines() if not line.startswith('#include '))


WAIT_BOUNDARIES = r'''
#define CONFIG_ESP32_MQUICKJS_WIFI_RADIO 1
#define configTICK_RATE_HZ 1000
#define portMAX_DELAY UINT32_MAX
typedef void *TaskHandle_t;
static TaskHandle_t current_task=(void *)1;
static TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current_task; }
'''


class WiFiStopWait(unittest.TestCase):
    def code(self):
        return netif_fixture.WiFiNetifRetirement().code(WAIT_BOUNDARIES + wait_source())

    def test_shared_budget_across_two_netifs_retains_late_cleanup_suffix(self):
        compile_run(self, self.code() + r'''
int main(void) {
    esp_netif_t ap={.alive=true},sta={.alive=true};
    esp_netif_t *a=&ap,*s=&sta;int ae=0,se=0;
    assert(esp32_mquickjs_wifi_wait_begin(4)==ESP_OK);
    assert(esp32_mquickjs_wifi_netif_retire(&a,&ae)==ESP_OK && !a && ticks==3);
    assert(esp32_mquickjs_wifi_wait_remaining(1000)==1);
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==ESP_ERR_TIMEOUT && ticks==4);
    assert(s && s_retire.netif==s && sta.detaches==1 && !se);
    uint32_t identity=s_retire.identity;int accepted=posts;
    esp32_mquickjs_wifi_wait_end();
    assert(esp32_mquickjs_wifi_wait_begin(4)==ESP_OK);
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==ESP_OK && !s);
    assert(s_retire.identity==identity && sta.detaches==1 && posts==accepted && ticks==6);
    esp32_mquickjs_wifi_wait_end();
    assert(esp32_mquickjs_wifi_wait_remaining(1000)==1000);
}
''')

    def test_task_isolation_nested_admission_expiration_wrap_and_tick_rounding(self):
        code = self.code().replace('#define configTICK_RATE_HZ 1000', '#define configTICK_RATE_HZ 100')
        compile_run(self, code + r'''
int main(void) {
    assert(esp32_mquickjs_wifi_wait_begin(0)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_wait_begin(60001)==ESP_ERR_INVALID_ARG);
    ticks=UINT32_MAX-2;
    assert(esp32_mquickjs_wifi_wait_begin(61)==ESP_OK);
    assert(esp32_mquickjs_wifi_wait_remaining(1000)==7);
    ticks+=4;assert(esp32_mquickjs_wifi_wait_remaining(1000)==3);
    assert(esp32_mquickjs_wifi_wait_begin(500)==ESP_ERR_INVALID_STATE);
    current_task=(void *)2;
    assert(esp32_mquickjs_wifi_wait_remaining(1000)==1000);
    assert(esp32_mquickjs_wifi_wait_begin(100)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_wait_end();
    current_task=(void *)1;
    assert(esp32_mquickjs_wifi_wait_remaining(1000)==3);
    ticks+=3;assert(esp32_mquickjs_wifi_wait_remaining(1000)==0);
    esp32_mquickjs_wifi_wait_end();
    assert(esp32_mquickjs_wifi_wait_begin(1)==ESP_OK);
    assert(esp32_mquickjs_wifi_wait_remaining(1000)==1);
    esp32_mquickjs_wifi_wait_end();
    assert(esp32_mquickjs_wifi_wait_remaining(17)==17);
}
''')


class WiFiStopCapture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        capture = ''.join(extract(CONFIG.read_text(), name) for name in (
            'wifi_capture_lifecycle_timeout', 'esp32_mquickjs_wifi_capture_stop'))
        cls.binary = build(cls.temp.name, options + capture, CAPTURE_MAIN)

    def test_strict_timeout_defaults_rooting_and_nth_allocation_failure(self):
        for expression, expected in [
            ('undefined', 1000), ('({})', 1000), ('({timeoutMs:undefined})', 1000),
            ('({timeoutMs:1})', 1), ('({timeoutMs:60000})', 60000),
            ('null', 0), ('[]', 0), ('({timeoutMs:0})', 0), ('({timeoutMs:-1})', 0),
            ('({timeoutMs:60001})', 0), ('({timeoutMs:1.5})', 0),
            ('({timeoutMs:4294967297})', 0), ('({timeoutMs:"100"})', 0),
            ('({timeoutMs:true})', 0), ('({timeoutMs:null})', 0),
            ('({timeoutMs:0/0})', 0), ('({timeoutMs:1/0})', 0),
            ('({"timeoutMs\\u0000":10})', 0), ('({force:true})', 0)]:
            run([str(self.binary), expression, str(expected)])


CAPTURE_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==3);unsigned expected=atoi(argv[2]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef ref;JSValue *root=JS_PushGCRef(ctx,&ref);
        *root=JS_Eval(ctx,argv[1],strlen(argv[1]),"stop",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        uint32_t timeout=0xdeadbeef;
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok=esp32_mquickjs_wifi_capture_stop(ctx,*root,&timeout);
        if(!nth){total=calls;assert(ok==(expected!=0));}
        if(ok)assert(timeout==expected && !JS_HasException(ctx));
        else { assert(timeout==0 && JS_HasException(ctx));JS_GetException(ctx); }
        inject=false;collect=false;JS_PopGCRef(ctx,&ref);JS_FreeContext(ctx);free(heap);test_ctx=NULL;
    }
}
'''
