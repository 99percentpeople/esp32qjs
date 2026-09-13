from tests.support.paths import ROOT as TEST_ROOT
import json
import re
from pathlib import Path

from tests.support.source_contract_test_case import SourceContractTestCase


ROOT = TEST_ROOT
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class FutureDriverCapacityArchitectureTests(SourceContractTestCase):
    def test_all_feature_runtime_has_capacity_for_every_native_driver(self):
        source = (
            MQUICKJS / "src/core/esp32_mquickjs_future.c"
        ).read_text(encoding="utf-8")
        match = re.search(
            r"#define ESP32_MQUICKJS_FUTURE_MAX_DRIVERS\s+(\d+)U", source
        )

        self.assertIsNotNone(match)
        # The reviewed union is an upper bound for any feature-gated ROM.
        # Counting the actual registration declarations avoids a stale comment
        # silently accepting a limit smaller than the newly added API surface.
        manifest = json.loads((ROOT / "api-manifest.json").read_text())
        registrations = [item for item in manifest["functions"] if item.get("futureRegistration")]
        self.assertGreaterEqual(int(match.group(1)), len(registrations))

    def test_capacity_failure_is_visible_in_boot_logs(self):
        source = (
            MQUICKJS / "src/core/esp32_mquickjs_future.c"
        ).read_text(encoding="utf-8")

        self.assertIn("Future driver registry exhausted", source)

    def test_runtime_reports_the_global_initialization_stage(self):
        source = (
            ROOT / "components/esp32qjs_runtime/src/esp32qjs_runtime.c"
        ).read_text(encoding="utf-8")

        self.assertIn("MQuickJS global initialization failed", source)


if __name__ == "__main__":
    import unittest

    unittest.main()
