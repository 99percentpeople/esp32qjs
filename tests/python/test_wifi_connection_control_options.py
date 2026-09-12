"""Deferred real public controls + coordinator/Radio with SDK boundaries and GC."""
import re
import tempfile
import unittest
from test_wifi_connection_controls import control_code
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from wireless_vm_fixture import CORE, build, extract, run


class WiFiConnectionControlOptions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        code = control_code('esp32c5/representative')
        code = re.sub(r'\bcalls\b', 'native_calls', code)
        code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        for name in ('tx_rate_interface', 'driver_phy_write_error', 'driver_connection_control',
                     'js_wifi_driver_set_inactive_time', 'js_wifi_driver_set_rssi_threshold'):
            code += extract(driver, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_input_bounds_arity_original_native_errors_and_error_gc(self):
        cases = [
            ('inactive', '["station",3]', True), ('inactive', '["station",65535]', True),
            ('inactive', '["access-point",10]', True), ('inactive', '["access-point",9]', False),
            ('inactive', '["station",2]', False), ('inactive', '["station",65536]', False),
            ('inactive', '["station",3.5]', False), ('inactive', '["station","3"]', False),
            ('inactive', '["station\\u0000",3]', False), ('inactive', '["station",3,4]', False),
            ('rssi', '[-100]', True), ('rssi', '[10]', True), ('rssi', '[0]', True),
            ('rssi', '[-101]', False), ('rssi', '[11]', False), ('rssi', '[-20.5]', False),
            ('rssi', '["-60"]', False), ('rssi', '[true]', False), ('rssi', '[]', False),
            ('rssi', '[-60,-70]', False), ('rssi', '[4294967295]', False),
        ]
        for kind, expression, expected in cases:
            with self.subTest(kind=kind, expression=expression):
                run([str(self.binary), kind, expression, str(int(expected)), '0'])
        for kind, expression in [('inactive', '["station",3]'), ('rssi', '[-75]')]:
            run([str(self.binary), kind, expression, '0', '1'])


MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==5);bool observer=!strcmp(argv[1],"rssi"),expected=atoi(argv[3]),sdk_fail=atoi(argv[4]);int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,result_ref,value_ref;
        JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&result_ref),*value=JS_PushGCRef(ctx,&value_ref);
        *input=JS_Eval(ctx,argv[2],strlen(argv[2]),"controls",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        *value=JS_GetPropertyStr(ctx,*input,"length");int32_t count;assert(!JS_ToInt32(ctx,&count,*value) && count<=4);
        JSValue args[4];for(int i=0;i<count;++i)args[i]=JS_GetPropertyUint32(ctx,*input,i);
        reset();if(sdk_fail)native_fail_at=observer ? 1 : 2;
        /* SDK call count and VM allocation count are independent injection axes. */
        calls=0;fail_at=nth;collect=true;inject=true;
        *output=observer ? js_wifi_driver_set_rssi_threshold(ctx,NULL,count,args) : js_wifi_driver_set_inactive_time(ctx,NULL,count,args);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!expected);}
        if(!expected && !sdk_fail)assert(writes==0);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && sdk_fail) {
                *output=JS_GetPropertyStr(ctx,*output,"details");*value=JS_GetPropertyStr(ctx,*output,"espCode");
                int32_t code;assert(!JS_ToInt32(ctx,&code,*value) && code==77);
                assert(s_radio.configuration.error==77 && s_radio.configuration.mutation_attempted);
            }
        } else {int32_t n;assert(!JS_ToInt32(ctx,&n,*output));int32_t requested;assert(!JS_ToInt32(ctx,&requested,args[observer ? 0 : 1]) && n==requested);}
        JS_PopGCRef(ctx,&value_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
'''
