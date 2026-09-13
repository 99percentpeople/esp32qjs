"""Deferred production SDK replacement; no runtime proof before Wi-Fi tests."""
from tests.support.fixtures import fixture_text
import importlib.util
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import ROOT


def source():
    path = ROOT / 'scripts/patch_idf_phy_antenna.py'
    spec = importlib.util.spec_from_file_location('antenna_gpio_patch', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return PREFIX + sdk_types('esp32c5/representative', ('esp_phy_ant_gpio_config_t',)) + BOUNDARIES + module.GPIO_WRITE


class WiFiAntennaGpioSdk(unittest.TestCase):
    def test_reserved_mask_and_complete_input_validation_before_mutation(self):
        compile_run(self, source() + fixture_text('wifi/config/test_wifi_antenna_gpio_sdk/test_reserved_mask_and_complete_input_validation_before_mutation.inc'))

    def test_gpio_error_does_not_connect_failed_pin_or_commit_saved_configuration(self):
        compile_run(self, source() + fixture_text('wifi/config/test_wifi_antenna_gpio_sdk/test_gpio_error_does_not_connect_failed_pin_or_commit_saved_configuration.inc'))

    def test_framework_owned_pins_skip_reclaim_and_propagate_output_enable_failure(self):
        compile_run(self, source() + fixture_text('wifi/config/test_wifi_antenna_gpio_sdk/test_framework_owned_pins_skip_reclaim_and_propagate_output_enable_failure.inc'))


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
typedef int esp_err_t;
'''

BOUNDARIES = fixture_text('wifi/config/test_wifi_antenna_gpio_sdk/boundaries.inc')
