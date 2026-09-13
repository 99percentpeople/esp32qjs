"""Production driver dump admission and MQuickJS diagnostics GC/OOM; deferred."""
from tests.support.fixtures import fixture_text

from pathlib import Path
import re
import tempfile
import unittest

from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run

BASE = ROOT / "components/esp32_mquickjs"


def native_code():
    header = (BASE / "internal/esp32_mquickjs_wifi_radio.h").read_text()
    state = re.search(r"typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_driver_state_t;", header).group(0)
    radio = (BASE / "src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c").read_text()
    return "#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>\n" + state + BOUNDARIES + extract(radio, "esp32_mquickjs_wifi_radio_dump_stats") + RESET


class WiFiDiagnosticsMethods(unittest.TestCase):
    def test_native_mask_admission_mutex_and_original_sdk_error(self):
        compile_run(self, native_code() + NATIVE_MAIN)

    def test_real_public_dump_validation_and_error_conversion_gc_oom(self):
        source = (BASE / "src/modules/wifi/esp32_mquickjs_wifi_diagnostics.c").read_text()
        code = native_code() + extract((CORE / "esp32_mquickjs.c").read_text(), "esp32_mquickjs_throw_native_error")
        for name in ("wifi_diagnostics_capture_mask", "js_wifi_diagnostics_dump_driver_stats"):
            code += extract(source, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, DUMP_MAIN)
            for scenario in ("success", "sdk-error", "admission", "invalid"):
                run([str(binary), scenario])

    def test_actual_generated_coverage_conversion_survives_gc_and_nth_failure(self):
        source = (BASE / "src/modules/wifi/esp32_mquickjs_wifi_diagnostics.c").read_text()
        code = COVERAGE_BOUNDARY + (BASE / "internal/esp32_mquickjs_wifi_coverage.inc").read_text()
        for name in ("wifi_coverage_counts_to_js", "wifi_coverage_rows_to_js", "js_wifi_diagnostics_idf_api_coverage"):
            code += extract(source, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, COVERAGE_MAIN)
            run([str(binary)])


BOUNDARIES = fixture_text('wifi/config/test_wifi_diagnostics_methods/boundaries.inc')
RESET = fixture_text('wifi/config/test_wifi_diagnostics_methods/reset.inc')
NATIVE_MAIN = fixture_text('wifi/config/test_wifi_diagnostics_methods/native_main.inc')
DUMP_MAIN = fixture_text('wifi/config/test_wifi_diagnostics_methods/dump_main.inc')
COVERAGE_BOUNDARY = fixture_text('wifi/config/test_wifi_diagnostics_methods/coverage_boundary.inc')
COVERAGE_MAIN = fixture_text('wifi/config/test_wifi_diagnostics_methods/coverage_main.inc')
