"""Deferred real VM EAP capture, GC, byte leases and Nth allocation failure.

Production options, ByteView, profile builder and secure-zero bodies. Only native
allocation/RTOS lock boundaries are injected. No SDK/Radio/authentication model.
"""
from tests.support.fixtures import fixture_text
from pathlib import Path
import tempfile
import unittest
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit, INTERNAL
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run


class WiFiEAPCaptureGC(unittest.TestCase):
    def test_getter_gc_allocation_failure_and_native_secret_cleanup(self):
        sdk = Path('/home/zach/esp/esp-idf/components/wpa_supplicant/esp_supplicant/include/esp_eap_client.h')
        folder = ROOT / 'components/esp32_mquickjs/src/modules/wifi_enterprise'
        base = '{methods:["peap"],username:"u",password:[1,0,2],caCertificate:view}'
        cases = [(base, True),
            ('{methods:["peap"],username:"ü",caCertificate:view}', True),
            ('{methods:["peap"],username:"u",defaultCertificateBundle:true}', True),
            ('{methods:["peap"],username:"u"}', False),
            (base.replace('["peap"]','["peap","peap"]'), False),
            (base.replace('["peap"]','["peap\\x00"]'), False),
            (base.replace('["peap"]','[]'), False),
            (base.replace('password:[1,0,2]', 'password:{length:4294967295}'), False),
            (base.replace('password:[1,0,2]', 'password:[1,256]'), False),
            (base.replace('password:[1,0,2]', 'password:[1,1.5]'), False),
            (base.replace('password:[1,0,2]', 'password:[1,undefined,2]'), False),
            (base.replace('password:[1,0,2]', 'password:null'), False),
            (base.replace('password:[1,0,2]', 'get password(){gc();return [1,0,2];}'), True),
            (base.replace('password:[1,0,2]', 'get password(){throw 12345;}'), False),
            (base.replace('password:[1,0,2]', 'password:{length:2,get 0(){gc();return 1;},get 1(){throw 12345;}}'), False),
            (base.replace('password:[1,0,2]', 'password:{length:1,get 0(){view.close();gc();return 1;}}'), False),
            (base.replace('password:[1,0,2]', 'password:{length:1,get 0(){this[1]=255;gc();return 1;}}'), True),
            (base.replace('username:"u"', 'get username(){view.close();return "u";}'), False),
            (base.replace('caCertificate:view', 'caCertificate:view,domain:"a\\x00b"'), False),
            (base.replace('caCertificate:view', 'caCertificate:view,checkCertificateTime:1'), False),
            (base.replace('caCertificate:view', 'caCertificate:view,okc:"true"'), False),
            (base.replace('caCertificate:view', 'caCertificate:view,fast:{}'), False),
            (base.replace('caCertificate:view', 'caCertificate:view,unknown:true'), False),
            ('{methods:["tls"],caCertificate:view,clientCertificate:view,privateKey:view,privateKeyPassword:"p\\x00q"}', False),
            ('(function(){var reads=0;return {methods:["peap"],username:"u",caCertificate:view,password:{get length(){if(++reads!==1)throw 12345;gc();return 1;},get 0(){gc();return 9;}}};})()', True),
        ]
        extra = PRELUDE + unit(sdk) + unit(INTERNAL / 'esp32_mquickjs_wifi_enterprise_profile.h')
        extra += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        extra += ALLOCATOR
        # Profile allocation reaches the heap through the managed allocator;
        # retirement now calls payload_free directly. Track that same boundary
        # so secret-zero verification runs before the actual native free.
        extra += '#define heap_caps_calloc eap_profile_alloc\n#define esp32_mquickjs_memory_payload_free eap_profile_free\n'
        extra += unit(folder / 'esp32_mquickjs_wifi_enterprise_profile.c')
        extra += '#undef heap_caps_calloc\n#undef esp32_mquickjs_memory_payload_free\n'
        extra += unit(CORE / 'esp32_mquickjs_options.c')
        extra += unit(folder / 'esp32_mquickjs_wifi_eap_options.c')
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, extra, MAIN)
            for expression, valid in cases:
                with self.subTest(expression=expression):
                    run([str(binary), '(' + expression + ')', str(int(valid)), str(int('12345' in expression and not valid))])


PRELUDE = fixture_text('wifi/security/test_wifi_eap_capture_gc/prelude.inc')
ALLOCATOR = fixture_text('wifi/security/test_wifi_eap_capture_gc/allocator.inc')
MAIN = fixture_text('wifi/security/test_wifi_eap_capture_gc/main.inc')
