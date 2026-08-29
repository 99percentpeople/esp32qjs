import re
from pathlib import Path

from source_contract_test_case import SourceContractTestCase


ROOT = Path(__file__).resolve().parents[2]
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
        # The current all-feature runtime registers 91 distinct native methods.
        # Keep deliberate headroom so adding one method cannot brick startup.
        self.assertGreaterEqual(int(match.group(1)), 128)

    def test_capacity_failure_is_visible_in_boot_logs(self):
        source = (
            MQUICKJS / "src/core/esp32_mquickjs_future.c"
        ).read_text(encoding="utf-8")

        self.assertIn("Future driver registry exhausted", source)

    def test_partial_worker_pool_failure_uses_tested_rollback_helper(self):
        source = (
            MQUICKJS / "src/core/esp32_mquickjs_future.c"
        ).read_text(encoding="utf-8")
        helper = (
            MQUICKJS / "src/core/esp32_mquickjs_future_worker_pool.c"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "esp32_mquickjs_future_worker_pool_cleanup_partial", source
        )
        self.assertIn("while (started_workers > 0)", helper)
        self.assertIn("cleanup_queue(opaque)", helper)

    def test_runtime_reports_the_global_initialization_stage(self):
        source = (
            ROOT / "components/esp32qjs_runtime/src/esp32qjs_runtime.c"
        ).read_text(encoding="utf-8")

        self.assertIn("MQuickJS global initialization failed", source)


if __name__ == "__main__":
    import unittest

    unittest.main()
