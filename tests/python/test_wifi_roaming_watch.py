"""Deferred scoped watch options/GC and production subscriber filtering.

Uses the shared production parser, SDK descriptors, mask and capture path.
Queue/lock/time boundaries come from watch_values. Does not stand in for full
EventQueue factory/close scheduling or RF evidence. Do not run before Wi-Fi gate.
"""
import tempfile
import unittest
from test_wifi_rx_target import unit
from test_wifi_watch_values import WATCH, watch_code
from wireless_vm_fixture import CORE, build, extract, run


class WiFiRoamingWatch(unittest.TestCase):
    def test_scope_options_gc_and_sdk_build_gate(self):
        source = WATCH.read_text()
        for rrm in (0, 1):
            code = watch_code()
            code += f'\n#undef CONFIG_ESP_WIFI_RRM_SUPPORT\n#define CONFIG_ESP_WIFI_RRM_SUPPORT {rrm}\n'
            code += '#define ESP32_MQUICKJS_WIFI_MAX_WATCH_CAPACITY 64\n'
            code += unit(CORE / 'esp32_mquickjs_options.c')
            code += extract(source, 'wifi_roaming_watch_mask')
            code += extract(source, 'wifi_watch_options')
            cases = [('undefined', 1), ('({})', 1), ('null', 0), ('[]', 0),
                     ('({events:"all"})', 1), ('({events:"all\\x00"})', 0),
                     ('({events:[]})', 0), ('({events:[undefined]})', 0),
                     ('({events:["WIFI_EVENT_STA_STOP"]})', 1),
                     ('({events:["WIFI_EVENT_STA_NEIGHBOR_REP"]})', rrm),
                     ('({events:["WIFI_EVENT_AP_START"]})', 0),
                     ('({events:["WIFI_EVENT_STA_STOP\\x00"]})', 0),
                     ('({events:["WIFI_EVENT_STA_STOP","WIFI_EVENT_STA_STOP"]})', 0),
                     ('({capacity:1})', 1), ('({capacity:64})', 1),
                     ('({capacity:0})', 0), ('({capacity:65})', 0),
                     ('({capacity:1.5})', 0), ('({capacity:"16"})', 0),
                     ('({overflow:"drop-newest"})', 1), ('({overflow:"drop-oldest"})', 0),
                     ('({includeRawEventData:false})', 0), ('({extra:1})', 0),
                     ('({get capacity(){gc();return 8;},get events(){gc();return ["WIFI_EVENT_STA_STOP"];}})', 1),
                     ('({get events(){gc();throw new Error("sentinel");}})', 0),
                     # The bundled VM does not invoke this indexed getter;
                     # reading the existing array slot produces undefined. Actual
                     # options getters and array reads above still run with GC.
                     ('(function(){var a=[undefined];Object.defineProperty(a,"0",{get:function(){gc();return "WIFI_EVENT_STA_STOP";}});return {events:a};})()', 0),
                     ('(function(){Object.defineProperty(Object.prototype,"includeRawEventData",{get:function(){throw new Error("must not read");}});return {};})()', 1)]
            with tempfile.TemporaryDirectory() as tmp:
                binary = build(tmp, code, MAIN)
                for expression, valid in cases:
                    with self.subTest(rrm=rrm, expression=expression):
                        run([str(binary), expression, str(valid), 'scoped'])
                for expression in ['({includeRawEventData:true,events:"all"})',
                                   '({events:["WIFI_EVENT_AP_START"]})']:
                    run([str(binary), expression, '1', 'generic'])


MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==4);bool expected=atoi(argv[2]),scoped=!strcmp(argv[3],"scoped");
    int total=0;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;JSGCRef arg_ref;JSValue *arg=JS_PushGCRef(ctx,&arg_ref);
        *arg=JS_Eval(ctx,argv[1],strlen(argv[1]),"watch-options",JS_EVAL_RETVAL);assert(!JS_IsException(*arg));
        wifi_watch_source_t parsed,unchanged;memset(&parsed,0xa5,sizeof(parsed));unchanged=parsed;
        uint32_t capacity=999;uint64_t allowed=wifi_roaming_watch_mask();
        calls=0;fail_at=nth;inject=collect=true;
        bool ok=wifi_watch_options(ctx,1,arg,scoped?"wifi.roaming.watch":"wifi.watch",scoped,allowed,&parsed,&capacity);
        if(!nth){total=calls;assert(ok==expected);}
        inject=collect=false;
        if(!ok) {
            assert(JS_HasException(ctx));(void)JS_GetException(ctx);
            assert(!memcmp(&parsed,&unchanged,sizeof(parsed)) && capacity==999);
        } else {
            assert(capacity>=1 && capacity<=64);
            if(scoped)assert(!parsed.all && !parsed.raw && parsed.mask && !(parsed.mask&~allowed));
            /* Capture and poll every SDK descriptor through the shared broker. */
            source.all=parsed.all;source.raw=parsed.raw;source.mask=parsed.mask;
            for(size_t i=0;i<sizeof(s_descriptors)/sizeof(s_descriptors[0]);i++) {
                int id=s_descriptors[i].id;sends=wakes=subscriber_sends=0;queued_ready=false;
                esp32_mquickjs_wifi_watch_capture(id,NULL,91);
                bool selected=parsed.all || (parsed.mask&(UINT64_C(1)<<id));
                queued_ready=sends!=0;
                wifi_watch_poll(ctx,source.runtime,NULL);
                assert(subscriber_sends==(int)selected && !queued_ready && !locked);
                if(selected)assert(subscriber_events[0].raw_requested==parsed.raw);
            }
        }
        JS_PopGCRef(ctx,&arg_ref);JS_FreeContext(ctx);free(heap);assert(!root_count && !native_live);
    }
    return 0;
}
'''
