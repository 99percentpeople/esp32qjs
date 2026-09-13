"""Production SoftAP parser, result conversion and native cleanup boundaries."""
from tests.support.fixtures import fixture_text
import pathlib
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run
from tests.support.native_compile import compile_run
AP=ROOT/'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_ap.c'

class WiFiAPCleanup(unittest.TestCase):
    def test_cleanup_retains_exclusion_and_retries_only_remaining_resources(self):
        body=extract(AP.read_text(),'wifi_ap_retire_netif')+extract(AP.read_text(),'wifi_ap_cleanup')
        compile_run(self,fixture_text('wifi/config/test_wifi_ap/test_cleanup_retains_exclusion_and_retries_only_remaining_resources-02.inc') +body+fixture_text('wifi/config/test_wifi_ap/test_cleanup_retains_exclusion_and_retries_only_remaining_resources.inc'))

# Full AP parser/validator/result VM coverage lives in test_wifi_ap_config.py.

class WiFiAPStartup(unittest.TestCase):
    def test_adapter_retains_accepted_configuration_and_wipes_caller_config(self):
        body=extract(AP.read_text(),'js_wifi_start_ap')
        compile_run(self,fixture_text('wifi/config/test_wifi_ap/test_adapter_retains_accepted_configuration_and_wipes_caller_config-02.inc')+body+fixture_text('wifi/config/test_wifi_ap/test_adapter_retains_accepted_configuration_and_wipes_caller_config.inc'))

class WiFiAPErrorFlags(unittest.TestCase):
    def test_restart_required_combines_driver_and_netif_failures(self):
        # Production converter, real movable-GC VM; only Radio query/error
        # envelope boundaries are replaced. This does not execute a Wi-Fi SDK.
        with tempfile.TemporaryDirectory() as directory:
            boundary = fixture_text('wifi/config/test_wifi_ap/test_restart_required_combines_driver_and_netif_failures-boundary.inc')
            main = fixture_text('wifi/config/test_wifi_ap/test_restart_required_combines_driver_and_netif_failures-main.inc')
            binary=build(directory,boundary+extract(AP.read_text(),'wifi_ap_error'),main)
            run([str(binary)])
