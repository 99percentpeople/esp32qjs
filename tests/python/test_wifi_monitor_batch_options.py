"""Deferred production Monitor Batch option parser in the actual moving VM."""
import tempfile
import unittest
from wireless_vm_fixture import build, run
from test_wifi_monitor_options import production_options_code


class WiFiMonitorBatchOptions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = build(cls.temp.name, production_options_code(0), MAIN)

    def check(self, expression, pool=16, valid=True):
        run([str(self.binary), expression, str(pool), str(int(valid))])

    def test_defaults_and_aggregation_limits(self):
        for pool in [1, 16, 32, 128]:
            self.check('undefined', pool)
            self.check('({})', pool)
            self.check('({maximumFrames:' + str(pool) + ',minimumFrames:' + str(pool) +
                       ',timeoutMs:0,maximumLatencyMs:2147483647})', pool)
        self.check('({maximumFrames:5,minimumFrames:3,timeoutMs:2147483647,maximumLatencyMs:0})')
        self.check('({timeoutMs:undefined,maximumLatencyMs:undefined})')

    def test_strict_fields_numbers_null_and_pool_bounds(self):
        for expression in ['null', 'true', '[]', '1', '"batch"', '({extra:1})',
                           '({"timeoutMs\\x00":1})', '({minimumFrames:3,maximumFrames:2})']:
            self.check(expression, valid=False)
        for name in ['maximumFrames', 'minimumFrames']:
            for value in ['0','17','-1','1.5','NaN','Infinity','null','true','"2"']:
                self.check('({' + name + ':' + value + '})', valid=False)
        for name in ['timeoutMs', 'maximumLatencyMs']:
            for value in ['-1','2147483648','0.5','NaN','Infinity','null','false','"0"']:
                self.check('({' + name + ':' + value + '})', valid=False)
        self.check('undefined', pool=0, valid=False)
        self.check('undefined', pool=129, valid=False)


MAIN = r'''
#undef JS_GetPropertyStr
#undef JS_GetPropertyUint32
int main(int argc,char **argv) {
    assert(argc==4);uint32_t pool=(uint32_t)atoi(argv[2]);bool expected=atoi(argv[3])!=0;int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);assert(heap);
        JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef r;JSValue *root=push_root(ctx,&r);
        *root=JS_Eval(ctx,argv[1],strlen(argv[1]),"batch options",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        esp32_mquickjs_wifi_monitor_batch_options_t result,before;
        memset(&result,0xa5,sizeof(result));memcpy(&before,&result,sizeof(before));
        calls=0;fail_at=nth;inject=collect=true;
        bool ok=esp32_mquickjs_wifi_monitor_capture_batch_options(ctx,*root,pool,&result);
        inject=collect=false;
        if(nth==0) {total=calls;assert(ok==expected);} else assert(!ok);
        if(!ok) {
            assert(JS_HasException(ctx));(void)JS_GetException(ctx);
            assert(!memcmp(&before,&result,sizeof(result)));
        } else {
            assert(result.maximum_frames>=1 && result.maximum_frames<=pool);
            assert(result.minimum_frames>=1 && result.minimum_frames<=result.maximum_frames);
            if(!strcmp(argv[1],"undefined") || !strcmp(argv[1],"({})")) {
                assert(result.maximum_frames==(pool<32?pool:32));
                assert(result.minimum_frames==1 && !result.timeout_set && !result.maximum_latency_ms);
            }
            if(strstr(argv[1],"timeoutMs:0"))assert(result.timeout_set && result.timeout_ms==0);
            if(strstr(argv[1],"timeoutMs:2147483647"))assert(result.timeout_set && result.timeout_ms==INT32_MAX);
        }
        assert(root_count==1 && !native_live);pop_root(ctx,&r);assert(!root_count);
        JS_FreeContext(ctx);free(heap);
    }
}
'''
