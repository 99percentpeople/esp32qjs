"""Deferred production NDP callback/timeout regressions; no execution on import.

The pinned SDK bodies and framework guards are compiled by the stage runner.
Only native driver, allocator, netif and scheduling boundaries are injected.
This does not substitute for the native archive, security handshake or RTOS/RF
integration tests. The before cases must fail the same lifetime assertions.
"""
from tests.support.fixtures import fixture_text
import unittest

from tests.c.integration.wifi.nan.test_idf_nan_control import NanNativeControl, BASE
from tests.support.wireless_vm_fixture import extract


class NanDatapathControl(unittest.TestCase):
    compile_run = NanNativeControl.compile_run
    sources = NanNativeControl.sources
    observer = NanNativeControl.observer

    def test_full_rx_null_data_and_timer_bodies_share_close_barrier(self):
        bridge = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_sdk.inc').read_text()
        code = self.observer() + fixture_text('wifi/nan/test_wifi_nan_datapath_control/test_full_rx_null_data_and_timer_bodies_share_close_barrier-code.inc')
        for name in ('__wrap_nan_rx_naf', '__wrap_nan_nulldata_txcb', 'esp32_mquickjs_wifi_nan_sdk_timer_process'):
            code += extract(bridge, name)
        self.compile_run(code + fixture_text('wifi/nan/test_wifi_nan_datapath_control/test_full_rx_null_data_and_timer_bodies_share_close_barrier.inc'))

    def test_confirm_event_oom_preserves_accepted_native_datapath(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            code = self.observer() + NDP_BOUNDARY + extract(source, 'nan_ndp_confirm_teardown')
            code += extract(source, 'nan_app_ndp_confirm_cb')
            self.compile_run(code + CONFIRM_OOM_MAIN, expect_failure=before)

    def test_rx_registration_failure_reports_original_error_and_retains_failed_cleanup(self):
        _, source = self.sources()
        code = self.observer() + NDP_BOUNDARY + extract(source, 'nan_ndp_confirm_teardown')
        code += extract(source, 'nan_app_ndp_confirm_cb')
        self.compile_run(code + fixture_text('wifi/nan/test_wifi_nan_datapath_control/test_rx_registration_failure_reports_original_error_and_retains_failed_cleanup.inc'))

    def test_successful_end_submission_does_not_free_inflight_host_security_record(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            code = self.observer() + NDP_BOUNDARY + extract(source, 'nan_ndp_confirm_teardown')
            self.compile_run(code + CONFIRM_STUB + fixture_text('wifi/nan/test_wifi_nan_datapath_control/test_successful_end_submission_does_not_free_inflight_host_security_record.inc'), expect_failure=before)

    def test_request_wait_timeout_does_not_free_native_request_record(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            code = self.observer() + NDP_BOUNDARY + REQUEST_BOUNDARY
            code += extract(source, 'esp_wifi_nan_datapath_req')
            self.compile_run(code + fixture_text('wifi/nan/test_wifi_nan_datapath_control/test_request_wait_timeout_does_not_free_native_request_record.inc'), expect_failure=before)

    def test_complete_naf_scope_survives_notice_and_is_frozen_before_stop(self):
        bridge = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_sdk.inc').read_text()
        self.compile_run(self.observer() + fixture_text('wifi/nan/test_wifi_nan_datapath_control/test_complete_naf_scope_survives_notice_and_is_frozen_before_stop-02.inc') + extract(bridge, '__wrap_nan_naf_txcb') + fixture_text('wifi/nan/test_wifi_nan_datapath_control/test_complete_naf_scope_survives_notice_and_is_frozen_before_stop.inc'))


NDP_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_datapath_control/ndp_boundary.inc')

CONFIRM_OOM_MAIN = fixture_text('wifi/nan/test_wifi_nan_datapath_control/confirm_oom_main.inc')

CONFIRM_STUB = r'''
static void nan_app_ndp_confirm_cb(uint8_t s,struct ndp_cb_peer_info*p,uint8_t own[6],uint8_t ip[8]){(void)s;(void)p;(void)own;(void)ip;}
'''

REQUEST_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_datapath_control/request_boundary.inc') + CONFIRM_STUB
