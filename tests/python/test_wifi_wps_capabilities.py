"""Deferred actual WPS capabilities converter with independent SDK build flags."""
import re
import tempfile
import unittest
from pathlib import Path
from wireless_vm_fixture import CORE, build, extract, run


class WpsCapabilities(unittest.TestCase):
    def test_softap_alone_does_not_report_registrar_and_public_role_matches_flag(self):
        source = (CORE.parent / 'modules/wifi_wps/esp32_mquickjs_wifi_wps.c').read_text()
        for softap, registrar, dualband in ((0, 0, 0), (1, 0, 0), (1, 1, 0), (1, 0, 1), (1, 1, 1)):
            code = f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {softap}\n#define CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR {registrar}\n#define CONFIG_SOC_WIFI_SUPPORT_5G {dualband}\n'
            code += '#define ESP32_MQUICKJS_WPS_CREDENTIALS_MAX 3\n#define ESP32_MQUICKJS_WPS_MAX_HANDLES 4\n#define WPS_WATCH_HANDLES 4\n#define WPS_WATCH_CAPACITY 16\n'
            code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
            code += extract(source, 'js_wifi_wps_capabilities')
            main = MAIN.replace('EXPECTED_REGISTRAR', str(registrar)).replace('EXPECTED_5GHZ', str(registrar and dualband))
            with self.subTest(softap=softap, registrar=registrar, dualband=dualband), tempfile.TemporaryDirectory() as tmp:
                run([str(build(tmp, code, main))])


MAIN = r'''
int main(void){
 void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);
 JSGCRef ref;JSValue *v=JS_PushGCRef(ctx,&ref);*v=js_wifi_wps_capabilities(ctx,NULL,0,NULL);assert(!JS_IsException(*v));
 assert(JS_GetPropertyStr(ctx,*v,"sdkApRegistrar")==JS_NewBool(EXPECTED_REGISTRAR));
 assert(JS_GetPropertyStr(ctx,*v,"apRegistrar")==JS_NewBool(EXPECTED_REGISTRAR));
 assert(JS_GetPropertyStr(ctx,*v,"apRegistrar5GHz")==JS_NewBool(EXPECTED_5GHZ));
 assert(JS_GetPropertyStr(ctx,*v,"enrollee")==JS_TRUE);
 JS_PopGCRef(ctx,&ref);JS_FreeContext(ctx);free(heap);return 0;
}
'''
