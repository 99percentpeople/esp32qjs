from pathlib import Path
import unittest

from source_contract_test_case import SourceContractTestCase


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class DmaTransferArchitectureTests(SourceContractTestCase):
    def setUp(self):
        self.spi = (
            MQUICKJS / "src/modules/spi/esp32_mquickjs_spi.c"
        ).read_text(encoding="utf-8")
        self.core = (
            MQUICKJS / "src/core/esp32_mquickjs_dma_transfer.c"
        ).read_text(encoding="utf-8")
        self.header = (
            MQUICKJS / "internal/esp32_mquickjs_dma_transfer.h"
        ).read_text(encoding="utf-8")

    def test_dma_core_is_driver_independent_and_has_two_rotating_slots(self):
        self.assertIn("ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT 2U", self.header)
        self.assertIn("esp32_mquickjs_dma_classify_source", self.core)
        self.assertIn("esp32_mquickjs_dma_cursor_next", self.core)
        self.assertIn("workspace->next_slot", self.core)
        self.assertIn("keep_cs_active", self.core)
        self.assertNotIn("spi_device_", self.core)
        self.assertNotIn("JSContext", self.core)

    def test_spi_bus_owns_one_fixed_staging_workspace(self):
        allocation = self.spi.split(
            "static bool spi_allocate_staging_workspace", 1
        )[1].split("static JSValue spi_open_bus", 1)[0]
        cleanup = self.spi.split(
            "static void spi_free_staging_workspace", 1
        )[1].split("static void spi_cleanup_device_slot", 1)[0]

        self.assertIn("staging_tx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT]", self.spi)
        self.assertIn("staging_rx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT]", self.spi)
        self.assertIn("esp32_mquickjs_memory_reserve_internal_dma", allocation)
        self.assertIn("esp32_mquickjs_memory_commit_staging_pinned", allocation)
        self.assertEqual(allocation.count("heap_caps_malloc("), 2)
        self.assertNotIn("realloc", allocation)
        self.assertIn("esp32_mquickjs_memory_release_driver_pinned", cleanup)

    def test_spi_resource_lane_acquire_and_queue_keep_runtime_cooperative(self):
        step = self.spi.split("static void spi_future_step(", 2)[-1].split(
            "static bool spi_future_start", 1
        )[0]

        self.assertIn(
            "spi_device_acquire_bus(device->handle, portMAX_DELAY)", step
        )
        self.assertNotIn("spi_device_acquire_bus(device->handle, 0)", step)
        self.assertIn("spi_device_queue_trans(device->handle, transaction, 0)", step)
        self.assertIn("spi_device_get_trans_result", step)
        self.assertIn("SPI_TRANS_CS_KEEP_ACTIVE", step)
        self.assertIn("esp32_mquickjs_dma_workspace_release", step)
        self.assertNotIn("pdMS_TO_TICKS", step)

    def test_source_and_slots_outlive_queued_transactions(self):
        release = self.spi.split("static void spi_future_release(", 1)[1].split(
            "static esp32_mquickjs_future_driver_state_t *spi_future_allocate", 1
        )[0]
        step = self.spi.split("static void spi_future_step(", 2)[-1].split(
            "static bool spi_future_start", 1
        )[0]

        self.assertIn("state->span_source_opened", release)
        self.assertIn("esp32_mquickjs_byte_span_source_close", release)
        self.assertIn("state->single_owner_retained", release)
        self.assertIn("state->single_read_leased", release)
        self.assertIn("esp32_mquickjs_byte_view_release_read", release)
        self.assertIn("esp32_mquickjs_byte_view_acquire_read", self.spi)
        self.assertLess(
            step.index("spi_device_get_trans_result"),
            step.index("esp32_mquickjs_dma_workspace_release"),
        )
        self.assertIn("state->in_flight > 0", self.spi)
        self.assertIn("ESP32_MQUICKJS_CANCEL_REQUESTED", self.spi)

    def test_v1_status_stats_timeout_and_fault_contract_is_exact(self):
        for field in (
            '"requestedFreqHz"',
            '"actualFreqHz"',
            '"directExternalDma"',
            '"timeoutMs"',
            '"dmaStagingBytes"',
            '"faulted"',
            '"lastErrorCode"',
        ):
            self.assertIn(field, self.spi)
        for field in (
            '"bytes"',
            '"sourceSpans"',
            '"transactions"',
            '"path"',
            '"stagedBytes"',
            '"copyUs"',
            '"queueUs"',
            '"waitUs"',
            '"transferUs"',
            '"totalUs"',
            '"queueDepth"',
        ):
            self.assertIn(field, self.spi)
        for code in (
            "DMA_STAGING_NO_MEMORY",
            "DMA_TX_UNDERFLOW",
            "DMA_RX_OVERFLOW",
            "DMA_TRANSFER_TIMEOUT",
            "DMA_DEVICE_FAULTED",
        ):
            self.assertIn(code, self.spi)
        self.assertIn("esp32_mquickjs_dma_progress_timeout_us", self.spi)
        self.assertIn(".on_timeout = spi_future_on_timeout", self.spi)
        self.assertIn("device->faulted = true", self.spi)


if __name__ == "__main__":
    unittest.main()
