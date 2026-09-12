"""Actual AP clients options/IP helper/JS converter with VM GC/OOM injection.

Run deferred to the combined Wi-Fi phase. SDK Radio list and lwIP DHCP/formatting
are explicit boundaries, not proof of RF association or lease freshness.
"""
import tempfile
import unittest
from wireless_vm_fixture import ROOT, CORE, build, extract, run

BOUNDARIES = r'''
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_NOT_SUPPORTED -3
#define ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS 4
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STR(a) (a)[0],(a)[1],(a)[2],(a)[3],(a)[4],(a)[5]
typedef int esp_err_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { uint8_t mac[6];esp_ip4_addr_t ip; } esp_netif_pair_mac_ip_t;
typedef struct { uint8_t mac[6];int rssi;bool phy_11b,phy_11g,phy_11n,phy_11a,phy_11ac,phy_11ax,phy_lr; } wifi_sta_info_t;
typedef struct { struct { int num;wifi_sta_info_t sta[4]; } stations;uint16_t aid[4]; } esp32_mquickjs_wifi_ap_clients_t;
static bool s_ap_cleanup_pending;
static int s_ap_lease;
static void *s_ap_netif=(void *)1;
static const char *s_ap_stage;
static int scenario,radio_calls,dhcp_calls,error_code;
static int esp32_mquickjs_wifi_radio_ap_clients(const void *lease,esp32_mquickjs_wifi_ap_clients_t *s) {
    assert(lease==&s_ap_lease);radio_calls++;memset(s,0,sizeof(*s));
    if(scenario==1)return -7;
    s->stations.num=scenario==2?0:2;
    for(int i=0;i<s->stations.num;i++) {
        s->stations.sta[i].mac[0]=2;s->stations.sta[i].mac[5]=i+1;
        s->stations.sta[i].rssi=-40-i;s->stations.sta[i].phy_11n=true;s->aid[i]=i?0:17;
    }
    return 0;
}
static int esp_netif_dhcps_get_clients_by_mac(void *netif,int num,esp_netif_pair_mac_ip_t *p) {
    assert(netif==(void *)1 && num==2);dhcp_calls++;
    assert(p[0].mac[0]==2 && p[0].mac[5]==1 && p[1].mac[5]==2);
    for(int i=0;i<4;i++)assert(!p[i].ip.addr);
    if(scenario==3)return -8;
    p[0].ip.addr=0x12345678; /* Missing second MAC deliberately leaves output unchanged. */
    return 0;
}
static char *esp_ip4addr_ntoa(const esp_ip4_addr_t *ip,char *text,int size) {
    assert(ip->addr==0x12345678 && size==16);
    if(scenario==4)return NULL;
    strcpy(text,"192.168.4.2");return text;
}
static JSValue wifi_ap_error(JSContext *ctx,const char *op,int code) {
    assert(!strcmp(op,"wifi.apClients"));error_code=code;
    return JS_ThrowTypeError(ctx,"native AP clients error");
}
'''

MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==6);int expected=atoi(argv[2]),args=atoi(argv[3]);scenario=atoi(argv[4]);
    bool include=atoi(argv[5]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;JSGCRef options_ref,result_ref;
        JSValue *options=JS_PushGCRef(ctx,&options_ref),*result=JS_PushGCRef(ctx,&result_ref);
        *options=JS_Eval(ctx,argv[1],strlen(argv[1]),"options",JS_EVAL_RETVAL);assert(!JS_IsException(*options));
        radio_calls=dhcp_calls=error_code=0;s_ap_stage="retained";s_ap_cleanup_pending=scenario==5;
        calls=0;fail_at=nth;collect=true;inject=true;
        *result=js_wifi_ap_clients(ctx,NULL,args,options);
        if(!nth) {total=calls;assert(!JS_IsException(*result)==expected);}
        inject=false;collect=false;
        if(JS_IsException(*result))JS_GetException(ctx);
        else {
            uint32_t length;assert(!JS_ToUint32(ctx,&length,JS_GetPropertyStr(ctx,*result,"length")));
            assert(length==(scenario==2?0:2));
            for(uint32_t i=0;i<length;i++) {
                JSValue entry=JS_GetPropertyUint32(ctx,*result,i),ip=JS_GetPropertyStr(ctx,entry,"ip");
                if(!include)assert(JS_IsUndefined(ip));
                else if(i==1)assert(JS_IsNull(ip));
                else {JSCStringBuf b;assert(!strcmp(JS_ToCString(ctx,ip,&b),"192.168.4.2"));}
                JSValue address=JS_GetPropertyStr(ctx,entry,"address");JSCStringBuf b;
                assert(!strcmp(JS_ToCString(ctx,address,&b),i?"02:00:00:00:00:02":"02:00:00:00:00:01"));
            }
        }
        if(!nth) {
            if(args>1 || scenario==5 || !expected && scenario==0)assert(!radio_calls && !dhcp_calls);
            if(!include || scenario==2 || !CONFIG_LWIP_DHCPS)assert(!dhcp_calls);
            if(scenario==3)assert(error_code==-8 && !strcmp(s_ap_stage,"clients-ip"));
            if(scenario==5)assert(!strcmp(s_ap_stage,"retained"));
            if(include && !CONFIG_LWIP_DHCPS)assert(error_code==ESP_ERR_NOT_SUPPORTED);
        }
        JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&options_ref);
        assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
    }
}
'''


class WiFiAPClientIPs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        options=(CORE/'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (ROOT/'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"',''))
        ap=(ROOT/'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_ap.c').read_text()
        body=''.join(extract(ap,n) for n in ['wifi_ap_client_result','wifi_ap_capture_clients_options',
                                           'wifi_ap_client_ips','js_wifi_ap_clients'])
        cls.binaries={enabled:build(cls.temp.name+'/'+str(enabled),
                                   '#define CONFIG_LWIP_DHCPS '+str(enabled)+'\n'+options+BOUNDARIES+body,MAIN)
                      for enabled in (0,1)}

    def check_case(self,options,expected=True,args=1,scenario=0,include=False,dhcp=1):
        run([str(self.binaries[dhcp]),options,str(int(expected)),str(args),str(scenario),str(int(include))])

    def test_optional_ip_and_unknown_lease(self):
        for options,args in [('undefined',0),('undefined',1),('({})',1),('({includeIp:false})',1)]:
            self.check_case(options,args=args)
        self.check_case('({includeIp:true})',include=True)
        self.check_case('({includeIp:true})',include=True,scenario=2)

    def test_prevalidation_and_sdk_errors(self):
        for options in ['null','[]','1','({includeIp:1})','({includeIp:"true"})',
                        '({extra:true})','({"includeIp\\u0000":true})']:
            self.check_case(options,False)
        self.check_case('({})',False,args=2)
        self.check_case('({includeIp:true})',False,include=True,scenario=1)
        self.check_case('({includeIp:true})',False,include=True,scenario=3)
        self.check_case('({includeIp:true})',False,include=True,scenario=4)
        self.check_case('({includeIp:true})',False,include=True,scenario=5)

    def test_no_dhcp_support_keeps_default_list_and_rejects_ip_before_radio(self):
        self.check_case('({})',dhcp=0)
        self.check_case('({includeIp:true})',False,include=True,dhcp=0)
