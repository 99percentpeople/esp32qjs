"""Deferred control/secret/role regressions against the production Mesh SDK.

Only AST-checked during implementation; execute with the consolidated Wi-Fi
suite. The fixture injects SDK calls, never a replacement control state machine.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.mesh.test_wifi_mesh_sdk import source
from tests.c.integration.wifi.mesh.test_wifi_mesh_session import source as session_source
from tests.support.native_compile import compile_run


class WiFiMeshControls(unittest.TestCase):
    def test_configuration_secrets_are_opt_in_and_layer_getter_requires_parent(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_controls/test_configuration_secrets_are_opt_in_and_layer_getter_requires_parent.inc'))

    def test_partial_ie_update_reports_completed_prefix_and_scrubs_input(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_controls/test_partial_ie_update_reports_completed_prefix_and_scrubs_input.inc'))

    def test_native_prevalidation_and_role_denial_do_not_submit_mutations(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_controls/test_native_prevalidation_and_role_denial_do_not_submit_mutations.inc'))

    def test_native_truncation_and_false_success_are_not_reported_as_applied(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_controls/test_native_truncation_and_false_success_are_not_reported_as_applied.inc'))

    def test_extended_job_owns_aligned_copy_and_retains_parent_after_capture(self):
        compile_run(self, session_source() + fixture_text('wifi/mesh/test_wifi_mesh_controls/test_extended_job_owns_aligned_copy_and_retains_parent_after_capture.inc'))
