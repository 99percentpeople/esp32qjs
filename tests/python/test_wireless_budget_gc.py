"""Actual sys budget conversion with movable GC and Nth allocation failure.

Only VM execution is scheduled here; native admission has its production-manager
fixture in tests/c/test_memory_wireless.c. Execution is deferred to Wi-Fi review.
"""
import tempfile
import unittest
from wireless_vm_fixture import CORE, build, extract, run


class WirelessBudgetGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (CORE / 'esp32_mquickjs_sys.c').read_text()
        code = (CORE / 'esp32_mquickjs_memory_budget.c').read_text()
        code += extract(source, 'sys_wireless_budget_region')
        code += extract(source, 'sys_wireless_budget')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_every_conversion_allocation_and_movable_gc(self):
        for collect in (0, 1):
            run([str(self.binary), str(collect)])


MAIN = r'''
int main(int argc,char **argv) {
    (void)argc;
    int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(128*1024);
        JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);
        assert(ctx);test_ctx=ctx;
        calls=0;fail_at=nth;collect=atoi(argv[1]);inject=false;moved_roots=0;
        esp32_mquickjs_memory_budget_t budget;
        assert(esp32_mquickjs_memory_budget_init(&budget,4096,8192,512));
        const size_t pool[2]={2048,4096};
        assert(esp32_mquickjs_memory_budget_reserve(&budget,ESP32_MQUICKJS_MEMORY_BUDGET_POOL,pool));
        assert(esp32_mquickjs_memory_budget_retire(&budget,pool));
        budget.rejected=UINT32_MAX;
        JSGCRef result_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref);
        inject=true;*result=sys_wireless_budget(ctx,&budget);inject=false;
        if(nth==0)total=calls;
        if(nth) {
            assert(JS_IsException(*result) && JS_HasException(ctx));
            (void)JS_GetException(ctx);
        } else {
            assert(!JS_IsException(*result) && !JS_HasException(ctx));
            if(collect)assert(moved_roots>0);
            uint32_t value;
            JSValue region=JS_GetPropertyStr(ctx,*result,"internal");
            JSValue roles=JS_GetPropertyStr(ctx,region,"roles");
            assert(!JS_ToUint32(ctx,&value,JS_GetPropertyStr(ctx,roles,"retiredPool")) && value==2048);
            assert(!JS_ToUint32(ctx,&value,JS_GetPropertyStr(ctx,roles,"pool")) && value==0);
            assert(!JS_ToUint32(ctx,&value,JS_GetPropertyStr(ctx,region,"controlReserveBytes")) && value==512);
            assert(!JS_ToUint32(ctx,&value,JS_GetPropertyStr(ctx,*result,"rejectedReservations")) && value==UINT32_MAX);
        }
        assert(budget.regions[0].reserved==2048 && budget.regions[1].reserved==4096);
        JS_PopGCRef(ctx,&result_ref);
        JS_FreeContext(ctx);free(heap);assert(native_live==0);
    }
    return 0;
}
'''
