"""Deferred NAN data admission and independent retirement on the production pool.

Native EB layout is supplied by a Host boundary; target ABI/link and RF proof
remain separate. These cases are authored, not executed during API development.
"""
from tests.support.fixtures import fixture_text
import unittest

from tests.c.integration.wifi.nan.test_wifi_nan_tx import NanTx


class NanDataRetirement(unittest.TestCase):
    compile_case = NanTx.compile_case

    def test_data_quota_keeps_management_capacity_and_recycles_only_rejected_transfer(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_data/test_data_quota_keeps_management_capacity_and_recycles_only_rejected_transfer.inc'), native=HELPERS)

    def test_native_delete_and_recycler_return_do_not_release_entered_callback(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_data/test_native_delete_and_recycler_return_do_not_release_entered_callback.inc'), native=HELPERS)

    def test_post_error_does_not_free_native_buffer_and_entered_post_retains_pool(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_data/test_post_error_does_not_free_native_buffer_and_entered_post_retains_pool.inc'), native=HELPERS)

    def test_duplicate_cannot_recycle_owner_and_old_recycler_cannot_retire_reused_address(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_data/test_duplicate_cannot_recycle_owner_and_old_recycler_cannot_retire_reused_address.inc'), native=HELPERS)

    def test_group_queue_stays_session_owned_and_wire_id_wrap_avoids_retained_ids(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_data/test_group_queue_stays_session_owned_and_wire_id_wrap_avoids_retained_ids.inc'), native=HELPERS)

    def test_individual_timer_cleanup_retries_only_delete_and_keeps_other_peer_setup(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_data/test_individual_timer_cleanup_retries_only_delete_and_keeps_other_peer_setup.inc'), native=HELPERS)


HELPERS = fixture_text('wifi/nan/test_wifi_nan_data/helpers.inc')
