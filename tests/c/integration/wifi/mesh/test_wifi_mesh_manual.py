"""Deferred production Mesh manual scan/parent ownership regressions.

Only parsed as Python AST during API implementation. SDK calls are injectable
boundaries; the real owner, scan helpers and Session job code are included.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.mesh.test_wifi_mesh_sdk import BASE, source
from tests.c.integration.wifi.mesh.test_wifi_mesh_session import source as session_source
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract

HELPERS = fixture_text('wifi/mesh/test_wifi_mesh_manual/helpers.inc')


class WiFiMeshManual(unittest.TestCase):
    def test_router_station_dhcp_and_disconnect_during_ip_read(self):
        radio = (BASE / 'src/modules/wifi_radio/esp32_mquickjs_wifi_mesh_radio.inc').read_text()
        functions = ''.join(extract(radio, name) for name in (
            'wifi_radio_mesh_dhcp_stop', 'wifi_radio_mesh_ip_parent', 'wifi_radio_mesh_network'))
        compile_run(self, source() + NETWORK + functions + HELPERS + fixture_text('wifi/mesh/test_wifi_mesh_manual/test_router_station_dhcp_and_disconnect_during_ip_read.inc'))

    def test_record_survives_failed_conversion_and_commits_only_exact_identity(self):
        compile_run(self, source() + HELPERS + fixture_text('wifi/mesh/test_wifi_mesh_manual/test_record_survives_failed_conversion_and_commits_only_exact_identity.inc'))


    def test_oom_bad_ie_length_and_identity_exhaustion_precede_destructive_read(self):
        compile_run(self, source() + HELPERS + fixture_text('wifi/mesh/test_wifi_mesh_manual/test_oom_bad_ie_length_and_identity_exhaustion_precede_destructive_read.inc'))

    def test_ambiguous_native_return_retains_arguments_and_quarantines_lane(self):
        compile_run(self, source() + HELPERS + fixture_text('wifi/mesh/test_wifi_mesh_manual/test_ambiguous_native_return_retains_arguments_and_quarantines_lane.inc'))

    def test_close_during_blocking_scan_waits_for_worker_return_and_flush_is_verified(self):
        compile_run(self, source() + HELPERS + fixture_text('wifi/mesh/test_wifi_mesh_manual/test_close_during_blocking_scan_waits_for_worker_return_and_flush_is_verified.inc'))

    def test_parent_layer_and_manual_admission_precede_native_mode_mutation(self):
        compile_run(self, source() + HELPERS + fixture_text('wifi/mesh/test_wifi_mesh_manual/test_parent_layer_and_manual_admission_precede_native_mode_mutation.inc'))

    def test_scan_reader_identity_and_copied_job_storage_survive_parent_close(self):
        compile_run(self, session_source() + fixture_text('wifi/mesh/test_wifi_mesh_manual/test_scan_reader_identity_and_copied_job_storage_survive_parent_close.inc'))


NETWORK = fixture_text('wifi/mesh/test_wifi_mesh_manual/network.inc')
