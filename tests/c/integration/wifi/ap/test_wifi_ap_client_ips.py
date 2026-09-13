"""Actual AP clients options/IP helper/JS converter with VM GC/OOM injection.

Run deferred to the combined Wi-Fi phase. SDK Radio list and lwIP DHCP/formatting
are explicit boundaries, not proof of RF association or lease freshness.
"""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run

BOUNDARIES = fixture_text('wifi/ap/test_wifi_ap_client_ips/boundaries.inc')

MAIN = fixture_text('wifi/ap/test_wifi_ap_client_ips/main.inc')


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
