"""Production AP sampling and rooted JS status conversion; phase execution deferred."""
import re
import tempfile
import unittest
from test_wifi_config_controls import PRELUDE, RADIO, HEADER, sdk_types, structure
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import ROOT, CORE, build, extract, run

AP = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_ap.c'
CONFIG = AP.with_name('esp32_mquickjs_wifi_config.c')


class WiFiAPStatusNative(unittest.TestCase):
    def code(self):
        radio, header = RADIO.read_text(), HEADER.read_text()
        client = re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;', header).group(0)
        types = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
        types += 'typedef unsigned esp32_mquickjs_wifi_promiscuous_token_t;\n'
        types += client + structure(header, 'esp32_mquickjs_wifi_radio_lease_t')
        types += structure(radio, 'wifi_radio_live_lease_t')
        types += structure(header, 'esp32_mquickjs_wifi_ap_snapshot_t')
        zero = extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        return PRELUDE + sdk_types('esp32c5/representative') + types + NATIVE_BOUNDARIES + zero + ''.join(
            extract(radio, name) for name in ['wifi_radio_lease_valid', 'esp32_mquickjs_wifi_radio_sample_ap'])

    def test_exact_admission_sdk_failures_secret_wipe_and_current_channel(self):
        compile_run(self, self.code() + r'''
static void reset(void) {
    assert(!depth && !critical && !allocation);
    memset(&s_radio,0,sizeof(s_radio));s_radio.generation=9;s_radio.started=true;s_radio.event_live=WIFI_MODE_AP;
    s_radio.leases[0]=(wifi_radio_live_lease_t){.identity=7,.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP};
    calls=0;fail_at=0;fail_alloc=false;bad_length=false;bad_count=false;stop_during_channel=false;
    native_mode=WIFI_MODE_APSTA;
}
static void public_fields_zero(esp32_mquickjs_wifi_ap_snapshot_t snapshot) {
    snapshot.started=false;
    const unsigned char *p=(const unsigned char *)&snapshot;
    /* Compare fields, not struct padding passed by value. */
    (void)p;assert(!snapshot.ssid_len && !snapshot.hidden && !snapshot.channel && !snapshot.max_connections && !snapshot.client_count);
    for(int i=0;i<32;i++)assert(!snapshot.ssid[i]);for(int i=0;i<6;i++)assert(!snapshot.mac[i]);
}
int main(void) {
    esp32_mquickjs_wifi_radio_lease_t lease={.generation=9,.identity=7,.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,.acquired=true};
    esp32_mquickjs_wifi_ap_snapshot_t snapshot;const char *stage;
    reset();
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_OK);
    assert(snapshot.started && !stage && snapshot.channel==6 && snapshot.hidden && snapshot.client_count==2);
    assert(snapshot.ssid_len==4 && !memcmp(snapshot.ssid,"A\0B\xff",4) && snapshot.mac[0]==2);
    assert(snapshot.max_connections==4 && calls==5 && !allocation);
    for(int fail=1;fail<=5;fail++) {
        reset();fail_at=fail;
        assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==-40-fail);
        assert(snapshot.started && stage && calls==fail && !allocation);public_fields_zero(snapshot);
    }
    for(int invalid=0;invalid<7;invalid++) {
        reset();esp32_mquickjs_wifi_radio_lease_t candidate=lease;
        if(invalid==0)candidate.identity++;
        if(invalid==1)candidate.generation++;
        if(invalid==2)candidate.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA;
        if(invalid==3)s_radio.lifecycle.identity=8;
        if(invalid==4)s_radio.started=false;
        if(invalid==5)s_radio.fault_stage="stop-events";
        assert(esp32_mquickjs_wifi_radio_sample_ap(invalid==6?NULL:&candidate,&snapshot,&stage)==ESP_ERR_INVALID_STATE);
        assert(!calls && !allocation && snapshot.started && !strcmp(stage,"admission"));public_fields_zero(snapshot);
    }
    reset();fail_alloc=true;
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_ERR_NO_MEM && calls==1);
    assert(!strcmp(stage,"allocate") && !allocation);public_fields_zero(snapshot);
    reset();native_mode=WIFI_MODE_STA;
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_ERR_INVALID_STATE && calls==1);
    reset();bad_length=true;
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_ERR_INVALID_SIZE && calls==2);
    assert(!allocation);public_fields_zero(snapshot);
    reset();bad_length=2;
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_OK && snapshot.ssid_len==1 && snapshot.ssid[0]=='A');
    reset();bad_count=true;
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_ERR_INVALID_SIZE && calls==4);
    reset();bad_count=2;
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_ERR_INVALID_SIZE && calls==4);
    reset();stop_during_channel=true;
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_ERR_INVALID_STATE);
    assert(!snapshot.started && !strcmp(stage,"state") && !allocation);public_fields_zero(snapshot);
}
''')


NATIVE_BOUNDARIES = r'''
static struct {wifi_interface_t interface;uint32_t identity,generation;} s_tx_rate_lease;

#define ESP_ERR_INVALID_SIZE -7
#define ESP_WIFI_MAX_CONN_NUM 4
#define MALLOC_CAP_8BIT 1
/* SDK association-list boundary: only the consumed count is simulated here. */
typedef struct { int num; } wifi_sta_list_t;
typedef int wifi_second_chan_t;
static struct { int lock;unsigned generation;bool started;int event_live;
    const char *fault_stage,*cleanup_stage;struct { unsigned identity; } lifecycle;
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES]; } s_radio;
static int depth,critical,calls,fail_at;
static bool fail_alloc,stop_during_channel;
static int bad_count,bad_length;
static void *allocation;static size_t allocation_size;
static wifi_mode_t native_mode;
static void wifi_radio_operation_lock(void) { assert(!depth && !critical);depth=1; }
static void wifi_radio_operation_unlock(void) { assert(depth && !critical);depth=0; }
#define taskENTER_CRITICAL(p) do { (void)(p);assert(!critical);critical=1; } while(0)
#define taskEXIT_CRITICAL(p) do { (void)(p);assert(critical);critical=0; } while(0)
static int boundary(void) { assert(depth && !critical);calls++;return calls==fail_at?-40-calls:0; }
static void *heap_caps_calloc(size_t count,size_t size,int caps) {
    assert(depth && !critical && count==1 && caps==1 && !allocation);
    if(fail_alloc)return NULL;allocation_size=size;return allocation=calloc(count,size);
}
static void heap_caps_free(void *p) {
    assert(depth && !critical && p==allocation);
    for(size_t i=0;i<allocation_size;i++)assert(!((unsigned char *)p)[i]);
    free(p);allocation=NULL;
}
static int esp_wifi_get_mode(wifi_mode_t *mode) { *mode=native_mode;return boundary(); }
static int esp_wifi_get_config(wifi_interface_t interface,wifi_config_t *config) {
    assert(interface==WIFI_IF_AP);memset(config,0,sizeof(*config));
    memcpy(config->ap.ssid,"A\0B\xff",4);config->ap.ssid_len=bad_length==1?33:bad_length==2?0:4;
    memset(config->ap.password,0x55,sizeof(config->ap.password));
    config->ap.ssid_hidden=true;config->ap.max_connection=4;config->ap.channel=11;
    config->ap.authmode=WIFI_AUTH_WPA2_PSK;return boundary();
}
static int esp_wifi_get_mac(wifi_interface_t interface,uint8_t mac[6]) {
    assert(interface==WIFI_IF_AP);memset(mac,0,6);mac[0]=2;return boundary();
}
static int esp_wifi_ap_get_sta_list(wifi_sta_list_t *list) { list->num=bad_count==1?-1:bad_count==2?5:2;return boundary(); }
static int esp_wifi_get_channel(uint8_t *primary,wifi_second_chan_t *secondary) {
    *primary=6;*secondary=0;if(stop_during_channel)s_radio.event_live=WIFI_MODE_STA;return boundary();
}
'''


class WiFiAPStatusVM(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(); cls.addClassCleanup(cls.temp.cleanup)
        header = HEADER.read_text()
        body = VM_BOUNDARIES + structure(header, 'esp32_mquickjs_wifi_ap_snapshot_t') + VM_SAMPLE
        body += extract(CONFIG.read_text(), 'esp32_mquickjs_wifi_ssid_text')
        body += ''.join(extract(AP.read_text(), name) for name in ['wifi_ap_auth_name', 'esp32_mquickjs_wifi_ap_status'])
        cls.binary = build(cls.temp.name, body, VM_MAIN)
        cls.text_binary = build(cls.temp.name + '/text', '#include "cutils.h"\n' +
                                extract(CONFIG.read_text(), 'esp32_mquickjs_wifi_ssid_text'), TEXT_MAIN)

    def test_binary_ssid_text_checks_exact_span_before_vm_string_construction(self):
        run([str(self.text_binary)])

    def test_absent_running_failed_closing_gc_and_allocation_failures(self):
        for scenario in range(5): run([str(self.binary), str(scenario)])


VM_BOUNDARIES = r'''
#include "cutils.h"
#define ESP_OK 0
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STR(a) (a)[0],(a)[1],(a)[2],(a)[3],(a)[4],(a)[5]
typedef int esp_err_t;
typedef enum { WIFI_AUTH_OPEN,WIFI_AUTH_WPA_PSK,WIFI_AUTH_WPA2_PSK,WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_WPA3_PSK,WIFI_AUTH_WPA2_WPA3_PSK,WIFI_AUTH_OWE } wifi_auth_mode_t;
typedef struct {uint32_t generation,identity;int client;bool acquired;} esp32_mquickjs_wifi_radio_lease_t;
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX 91
static struct {uint32_t generation,identity;} s_ap_raw_owner;
static void *s_ap_netif;
static struct { bool acquired; } s_ap_lease;
static bool s_ap_cleanup_pending;
static const char *s_ap_stage;
static int scenario,queries;
'''
VM_SAMPLE = r'''
static int esp32_mquickjs_wifi_radio_sample_ap(const void *lease,esp32_mquickjs_wifi_ap_snapshot_t *s,const char **stage) {
    assert(lease==(scenario==3?NULL:&s_ap_lease));queries++;
    memset(s,0,sizeof(*s));s->started=true;
    if(scenario==2 || scenario==3){*stage=scenario==3?"admission":"config";return -27;}
    s->ssid[0]='A';s->ssid[1]=0;s->ssid[2]=255;s->ssid_len=3;s->hidden=true;
    if(scenario==4){s->ssid[2]=0xc3;s->ssid[3]=0xa9;s->ssid_len=4;}
    s->channel=6;s->max_connections=4;s->client_count=2;s->mac[0]=2;s->authmode=WIFI_AUTH_WPA2_PSK;
    *stage=NULL;return 0;
}
'''
VM_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==2);scenario=atoi(argv[1]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        s_ap_netif=scenario?(void *)1:NULL;s_ap_lease.acquired=scenario!=0;
        s_ap_cleanup_pending=scenario==3;s_ap_stage="netif-detach";queries=0;
        JSGCRef ref;JSValue *result=JS_PushGCRef(ctx,&ref);
        calls=0;fail_at=nth;collect=true;inject=true;
        *result=esp32_mquickjs_wifi_ap_status(ctx);
        if(!nth){total=calls;assert(!JS_IsException(*result));}
        inject=false;collect=false;
        if(JS_IsException(*result))JS_GetException(ctx);
        else if(!scenario)assert(JS_IsNull(*result) && !queries);
        else {
            assert(queries==1 && JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"password")));
            JSValue bytes=JS_GetPropertyStr(ctx,*result,"ssidBytes");
            if(scenario==1 || scenario==4) {
                uint32_t n;assert(!JS_ToUint32(ctx,&n,JS_GetPropertyUint32(ctx,bytes,2)) && n==(scenario==4?0xc3:255));
                assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"ssid"))==(scenario==1));
                assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*result,"channel")) && n==6);
                assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*result,"clientCount")) && n==2);
                JSCStringBuf buffer;assert(!strcmp(JS_ToCString(ctx,JS_GetPropertyStr(ctx,*result,"mac"),&buffer),"02:00:00:00:00:00"));
                assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"queryError")));
            } else {
                assert(JS_IsNull(bytes) && JS_IsNull(JS_GetPropertyStr(ctx,*result,"ssid")));
                assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"clientCount")));
                int32_t err;assert(!JS_ToInt32(ctx,&err,JS_GetPropertyStr(ctx,*result,"queryError")) && err==-27);
            }
        }
        assert(!strcmp(s_ap_stage,"netif-detach"));
        JS_PopGCRef(ctx,&ref);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);test_ctx=NULL;
    }
}
'''

TEXT_MAIN = r'''
int main(void) {
    void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
    static const struct { uint8_t data[6];size_t length;bool valid; } cases[]={
        {{0},0,true},{{'A',0,'B'},3,true},{{0xc3,0xa9},2,true},
        {{0xf0,0x9f,0x98,0x80},4,true},{{0xff},1,false},
        {{0xc0,0x80},2,false},{{0xe2,0x82,0xac},2,false},
        {{0xed,0xa0,0x80},3,false},{{0xf4,0x90,0x80,0x80},4,false},
        {{0xe2,0x28,0xa1},3,false},{{0x80},1,false},{{0xe2,0x82,0xac,0xff},3,true},
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        JSValue text=esp32_mquickjs_wifi_ssid_text(ctx,cases[i].data,cases[i].length);
        assert(!JS_IsException(text));assert(JS_IsString(ctx,text)==cases[i].valid);
        if(!cases[i].valid)assert(JS_IsNull(text));
    }
    JS_FreeContext(ctx);free(heap);
}
'''
