"""Actual counter/reset/queue paths with native boundaries; execution deferred."""
from tests.support.fixtures import fixture_text

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.support.wireless_vm_fixture import ROOT, CORE, INTERNAL, build, extract, run
from tests.support.wifi_connection_counter_fixture import connection_counter_code


class WiFiCounterReset(unittest.TestCase):
    def compile_native(self, code, sources=(), includes=()):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as tmp:
            source, binary = Path(tmp) / "case.c", Path(tmp) / "case"
            source.write_text(code)
            result = subprocess.run([compiler, "-std=c11", "-pthread", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-function", *["-I" + str(path) for path in includes], str(source),
                *map(str, sources), "-o", str(binary)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_actual_enqueue_receive_overflow_isr_reset_and_peak_race(self):
        source = (CORE / "esp32_mquickjs_event_queue.c").read_text()
        start = source.index("struct esp32_mquickjs_event_queue {")
        end = source.index("struct esp32_mquickjs_future_driver_state {")
        header = (INTERNAL / "esp32_mquickjs_event_queue.h").read_text()
        code = QUEUE_TYPES + source[start:end]
        for name in ("esp32_mquickjs_event_queue_status_t", "esp32_mquickjs_event_queue_stats_t"):
            code += re.search(r"typedef struct \{[^}]*\} " + name + ";", header).group(0)
        code += QUEUE_BOUNDARIES
        for name in ("event_queue_runtime", "event_queue_drain_receive", "event_queue_sum",
                     "event_queue_enqueue_send", "event_queue_enqueue_send_from_isr",
                     "esp32_mquickjs_event_queue_send", "esp32_mquickjs_event_queue_try_send_from_callback",
                     "esp32_mquickjs_event_queue_try_receive", "esp32_mquickjs_event_queue_send_from_isr",
                     "esp32_mquickjs_event_queue_discard_all", "esp32_mquickjs_event_queue_get_stats",
                     "esp32_mquickjs_get_event_queue_status", "esp32_mquickjs_reset_event_queue_counters"):
            code += extract(source, name)
        self.compile_native(code + QUEUE_MAIN, [CORE / "esp32_mquickjs_event_queue_drain.c"], [INTERNAL])

    def test_connection_reset_does_not_forget_earlier_association_or_wrap(self):
        self.compile_native(CONNECTION_BOUNDARY + connection_counter_code() + CONNECTION_MAIN)

    def test_real_manager_reset_preserves_owned_and_retired_reservations(self):
        heap = (ROOT / "tests/c/unit/memory/test_memory_wireless.c").read_text().split("int main(void)")[0]
        sources = [CORE / ("esp32_mquickjs_" + name + ".c") for name in
                   ("memory", "memory_budget", "memory_owner_accounting", "memory_dma_accounting")]
        self.compile_native(heap + MEMORY_MAIN, sources, [ROOT / "tests/c/support/memory_stubs", INTERNAL])

    def test_public_reset_arity_runtime_no_allocation_and_metadata_conversion(self):
        source = (ROOT / "components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_diagnostics.c").read_text()
        native = re.search(r"typedef struct \{[^}]*\} wifi_diagnostics_reset_t;", source).group(0)
        native += "\nstatic wifi_diagnostics_reset_t s_diagnostics_reset;\n"
        for name in ("js_wifi_diagnostics_reset_counters", "wifi_diagnostics_reset_to_js"):
            native += extract(source, name)
        for enabled in (0, 1):
            with tempfile.TemporaryDirectory() as tmp:
                binary = build(tmp, f"#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI {enabled}\n" + RESET_BOUNDARIES + native, RESET_MAIN)
                run([str(binary)])


QUEUE_TYPES = fixture_text('wifi/config/test_wifi_counter_reset/queue_types.inc')
QUEUE_BOUNDARIES = fixture_text('wifi/config/test_wifi_counter_reset/queue_boundaries.inc')
QUEUE_MAIN = fixture_text('wifi/config/test_wifi_counter_reset/queue_main.inc')
CONNECTION_BOUNDARY = fixture_text('wifi/config/test_wifi_counter_reset/connection_boundary.inc')
CONNECTION_MAIN = fixture_text('wifi/config/test_wifi_counter_reset/connection_main.inc')
MEMORY_MAIN = fixture_text('wifi/config/test_wifi_counter_reset/memory_main.inc')
RESET_BOUNDARIES = fixture_text('wifi/config/test_wifi_counter_reset/reset_boundaries.inc')
RESET_MAIN = fixture_text('wifi/config/test_wifi_counter_reset/reset_main.inc')
