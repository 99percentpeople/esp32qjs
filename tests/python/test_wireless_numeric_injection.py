"""Numeric fault boundaries must agree with the linked MQuickJS representation."""
import tempfile
import unittest

from wireless_vm_fixture import build, run


MAIN = r'''
int main(void) {
    void *heap=malloc(128*1024);
    JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
    const double numbers[]={0,-0.0,255,-1073741824.0,1073741823.0,
        1073741824.0,0.5,1e200,1e-200,INFINITY,NAN};
    for(unsigned i=0;i<sizeof(numbers)/sizeof(*numbers);++i) {
        inject=collect=false;
        JSValue actual=JS_NewFloat64(ctx,numbers[i]);assert(!JS_IsException(actual));
        /* The only boxed numeric constant is the context's preallocated -0. */
        bool allocated=JS_IsPtr(actual) && numbers[i]!=0;
        calls=0;fail_at=1;inject=true;
        JSValue tested=new_float64(ctx,numbers[i]);inject=false;
        assert(calls==(int)allocated && JS_IsException(tested)==allocated);
        if(allocated)assert(JS_HasException(ctx)),(void)JS_GetException(ctx);
    }
    const int64_t integers[]={0,255,-1073741824,1073741823,1073741824,INT64_MAX};
    for(unsigned i=0;i<sizeof(integers)/sizeof(*integers);++i) {
        JSValue actual=JS_NewInt64(ctx,integers[i]);assert(!JS_IsException(actual));
        bool allocated=JS_IsPtr(actual);
        calls=0;fail_at=1;inject=true;
        JSValue tested=new_int64(ctx,integers[i]);inject=false;
        assert(calls==(int)allocated && JS_IsException(tested)==allocated);
        if(allocated)(void)JS_GetException(ctx);
        if(integers[i]>=0 && integers[i]<=UINT32_MAX) {
            calls=0;inject=true;tested=new_uint32(ctx,(uint32_t)integers[i]);inject=false;
            assert(calls==(int)allocated && JS_IsException(tested)==allocated);
            if(allocated)(void)JS_GetException(ctx);
        }
    }
    JSGCRef ref;JSValue *root=JS_PushGCRef(ctx,&ref);
    *root=JS_NewObject(ctx);assert(!JS_IsException(*root));
    calls=0;fail_at=0;inject=collect=true;
    assert(!JS_IsException(JS_SetPropertyUint32(ctx,*root,0,new_uint32(ctx,255))));
    assert(!calls); /* A byte constructor must not move the by-value owner. */
    assert(!JS_IsException(new_float64(ctx,1e200)) && calls==1);
    inject=collect=false;
    assert(JS_GetPropertyUint32(ctx,*root,0)==JS_NewUint32(ctx,255));
    JS_PopGCRef(ctx,&ref);JS_FreeContext(ctx);free(heap);
    assert(!native_live && !root_count);return 0;
}
'''


class WirelessNumericInjection(unittest.TestCase):
    def test_immediate_numbers_do_not_allocate_and_boxed_numbers_still_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            run([str(build(tmp, '', MAIN))])
