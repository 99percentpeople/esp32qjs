"""Immutable admission preflight and resolved Kconfig regression; deferred execution."""
from tests.support.paths import ROOT as TEST_ROOT

import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = TEST_ROOT
spec = importlib.util.spec_from_file_location("wireless_budget", ROOT / "scripts/build_tools/wireless_budget.py")
budget = importlib.util.module_from_spec(spec)
spec.loader.exec_module(budget)


class WirelessBudgetContext(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.manifest = {"schema": 1, "hardware": {"mcu": "esp32s3", "psramMode": "octal", "psramBytes": 8 << 20}}
        self.defaults = {
            "ESP32_MQUICKJS_FEATURE_WIFI": "y", "SPIRAM": "y",
            budget.PREFIX + "INTERNAL_BUDGET_BYTES": "131072",
            budget.PREFIX + "PSRAM_BUDGET_BYTES": "4194304",
            budget.PREFIX + "CONTROL_RESERVE_BYTES": "16384",
        }
        self.resolved = {"IDF_TARGET": "esp32s3", "SPIRAM": True,
                         "ESP32_MQUICKJS_PSRAM_MODE": "octal", "SPIRAM_MODE_OCT": True, "SPIRAM_MODE_QUAD": False,
                         "ESP32_MQUICKJS_FEATURE_WIFI": True,
                         "ESP32_MQUICKJS_FUTURE_WORKER_POOL_SIZE": 2,
                         **{name: int(self.defaults[name]) for name in budget.LIMITS}}

    def validate(self, resolved=True, extra=""):
        (self.directory / "manifest.json").write_text(json.dumps(self.manifest))
        (self.directory / "sdkconfig.defaults").write_text(
            "".join(f"CONFIG_{key}={value}\n" for key, value in self.defaults.items()) + extra)
        path = self.directory / "sdkconfig.json"
        path.write_text(json.dumps(self.resolved))
        return budget.validate_wireless_budget(self.directory, sdkconfig=path if resolved else None)

    def test_actual_defaults_and_resolved_agreement_without_native_features(self):
        self.assertTrue(self.validate(False)["wirelessEnabled"])
        result = self.validate()
        self.assertTrue(result["resolvedChecked"])
        self.assertEqual(result["limits"]["controlReserveBytes"], 16384)

    def test_missing_duplicate_invalid_or_overflowing_quotas(self):
        original = copy.deepcopy(self.defaults)
        for key, maximum in budget.LIMITS.items():
            for invalid in (None, "-1", "1.5", "true", "0x10000", "1e6", "01", str(maximum + 1), "9" * 100):
                self.defaults = copy.deepcopy(original)
                if invalid is None:
                    del self.defaults[key]
                else:
                    self.defaults[key] = invalid
                with self.subTest(key=key, value=invalid), self.assertRaises(ValueError):
                    self.validate(False)
            self.defaults = copy.deepcopy(original)
            with self.assertRaisesRegex(ValueError, "duplicate"):
                self.validate(False, f"CONFIG_{key}=1\n")

    def test_resolved_values_cannot_be_silently_clamped_or_reused(self):
        original = copy.deepcopy(self.resolved)
        for key in budget.LIMITS:
            for value in (None, False, "131072", self.resolved[key] + 1):
                self.resolved = copy.deepcopy(original)
                self.resolved[key] = value
                with self.subTest(key=key, value=value), self.assertRaisesRegex(ValueError, "differs"):
                    self.validate()

    def test_control_and_worker_lower_bound(self):
        for value in ("0", "131072", "131073"):
            self.defaults[budget.PREFIX + "CONTROL_RESERVE_BYTES"] = value
            with self.assertRaises(ValueError):
                self.validate(False)
        self.defaults[budget.PREFIX + "CONTROL_RESERVE_BYTES"] = "4096"
        self.defaults[budget.PREFIX + "INTERNAL_BUDGET_BYTES"] = "12287"
        self.resolved.update({key: int(self.defaults[key]) for key in budget.LIMITS})
        with self.assertRaisesRegex(ValueError, "worker stacks"):
            self.validate()

    def test_physical_psram_and_target_must_match(self):
        original = copy.deepcopy(self.manifest)
        for key, value in (("psramBytes", 1024), ("psramBytes", True), ("psramBytes", "8388608"),
                           ("psramMode", "none"), ("psramMode", "invalid"), ("mcu", "esp32c3")):
            self.manifest = copy.deepcopy(original)
            self.manifest["hardware"][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                self.validate()
        self.manifest = original
        self.resolved["SPIRAM"] = False
        with self.assertRaisesRegex(ValueError, "SPIRAM"):
            self.validate()
        self.resolved["SPIRAM"] = True
        self.resolved["SPIRAM_MODE_OCT"] = False
        with self.assertRaisesRegex(ValueError, "bus mode"):
            self.validate()

    def test_wireless_disabled_and_resolved_only_enable(self):
        self.defaults = {"ESP32_MQUICKJS_FEATURE_WIFI": "n"}
        self.resolved["ESP32_MQUICKJS_FEATURE_WIFI"] = False
        self.assertIsNone(self.validate()["limits"])
        self.resolved["ESP32_MQUICKJS_FEATURE_WIFI"] = True
        with self.assertRaisesRegex(ValueError, "explicit"):
            self.validate()
        self.resolved["ESP32_MQUICKJS_FEATURE_WIFI"] = False
        self.manifest["nativeFeatures"] = ["ble"]
        with self.assertRaisesRegex(ValueError, "explicit"):
            self.validate()

    def test_non_psram_context_cannot_reserve_external_memory(self):
        self.manifest["hardware"].update(psramMode="none", psramBytes=0)
        self.defaults["SPIRAM"] = "n"
        self.resolved.pop("SPIRAM")
        self.resolved["ESP32_MQUICKJS_PSRAM_MODE"] = "none"
        with self.assertRaisesRegex(ValueError, "physical PSRAM"):
            self.validate(False)
        self.defaults[budget.PREFIX + "PSRAM_BUDGET_BYTES"] = "0"
        self.resolved[budget.PREFIX + "PSRAM_BUDGET_BYTES"] = 0
        self.assertEqual(self.validate()["limits"]["psramBytes"], 0)
