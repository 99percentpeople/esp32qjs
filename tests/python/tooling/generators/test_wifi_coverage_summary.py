"""Reviewed-map summaries keep registration and implementation separate; deferred."""
from tests.support.paths import ROOT as TEST_ROOT

import copy
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = TEST_ROOT
sys.path.insert(0, str(ROOT / "scripts"))
try:
    spec = importlib.util.spec_from_file_location("wifi_coverage_generator", ROOT / "scripts/generate_idf_wifi_api_map.py")
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
finally:
    sys.path.pop(0)


class WiFiCoverageSummary(unittest.TestCase):
    def setUp(self):
        self.inventory = {"idfRevision": "reviewed", "headers": {"public.h": "hash"}, "variants": {
            "esp32c3/first": {"symbols": {"public.h::first": {"kind": "function"},
                                          "public.h::VALUE": {"kind": "macro"}}},
            "esp32c5/second": {"symbols": {"public.h::first": {"kind": "function"},
                                           "public.h::second": {"kind": "function"}}}}}
        self.mapping = {"tasks": {"W-07": {"disposition": "mapped", "implementation": "planned",
                                           "contract": "contract-pending"}}, "symbols": {
            "public.h::first": {"task": "W-07", "jsPath": "wifi.first", "implementation": "in-progress"},
            "public.h::second": {"task": "W-07", "jsPath": "wifi.absent"},
            "public.h::VALUE": {"task": "W-07", "disposition": "build-time", "contract": "reviewed"}},
            "unexpandedHeaders": {"omitted.h": "Conditional declaration gap, not unsupported."}}
        self.actual = {"functions": [{"qualifiedName": "wifi.first"}]}

    def test_union_defaults_overrides_and_registration_do_not_imply_completion(self):
        output = generator.runtime_summary(self.inventory, self.mapping, self.actual)
        self.assertIn("s_wifi_coverage_total[] = {3U, 2U, 1U, 2U, 0U, 1U, 0U, 0U, 0U, 2U, 1U, 0U, 0U, 2U, 1U}", output)
        self.assertIn('"esp32c3/first", {2U, 1U, 1U', output)
        self.assertIn('"esp32c5/second", {2U, 2U, 1U', output)
        self.assertIn('"omitted.h", "Conditional declaration gap, not unsupported."', output)
        self.assertIn("{NULL, NULL}", output)

    def test_registration_changes_only_its_count(self):
        self.actual["functions"].append({"qualifiedName": "wifi.absent"})
        output = generator.runtime_summary(self.inventory, self.mapping, self.actual)
        self.assertIn("s_wifi_coverage_total[] = {3U, 2U, 2U, 2U, 0U, 1U, 0U, 0U, 0U, 2U, 1U, 0U, 0U, 2U, 1U}", output)

    def test_conflicting_conditional_kind_requires_review(self):
        self.inventory["variants"]["esp32c5/second"]["symbols"]["public.h::first"]["kind"] = "macro"
        with self.assertRaisesRegex(ValueError, "symbol kind"):
            generator.runtime_summary(self.inventory, self.mapping, self.actual)

    def test_empty_gap_list_and_determinism(self):
        self.mapping["unexpandedHeaders"] = {}
        snapshot = copy.deepcopy(self.mapping)
        first = generator.runtime_summary(self.inventory, self.mapping, self.actual)
        self.assertEqual(first, generator.runtime_summary(self.inventory, self.mapping, self.actual))
        self.assertEqual(self.mapping, snapshot)
        self.assertIn("{NULL, NULL}", first)
