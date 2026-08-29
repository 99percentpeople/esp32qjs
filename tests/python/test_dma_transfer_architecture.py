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
        )[1].split("static int spi_resource_remove_device", 1)[0]

        self.assertIn("staging_tx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT]", self.spi)
        self.assertIn("staging_rx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT]", self.spi)
        self.assertIn("esp32_mquickjs_memory_reserve_internal_dma", allocation)
        self.assertIn("esp32_mquickjs_memory_commit_staging_pinned", allocation)
        self.assertIn(
            "esp32_mquickjs_dma_workspace_allocate_buffers", allocation
        )
        self.assertNotIn("heap_caps_malloc(", allocation)
        self.assertNotIn("realloc", allocation)
        self.assertIn(
            "esp32_mquickjs_dma_workspace_release_buffers", cleanup
        )
        self.assertIn("esp32_mquickjs_memory_release_driver_pinned", cleanup)

    def test_spi_native_teardown_retains_failed_device_and_bus_resources(self):
        resources = (
            MQUICKJS
            / "src/modules/spi/esp32_mquickjs_spi_native_resources.c"
        ).read_text(encoding="utf-8")
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")
        device_cleanup_start = self.spi.index(
            "static esp_err_t spi_cleanup_device_slot("
        )
        device_cleanup_end = self.spi.index(
            "\nstatic esp_err_t spi_cleanup_bus_slot(", device_cleanup_start
        )
        device_cleanup = self.spi[device_cleanup_start:device_cleanup_end]
        bus_cleanup_start = device_cleanup_end + 1
        bus_cleanup_end = self.spi.index(
            "\nstatic esp_err_t spi_cleanup_all(void)", bus_cleanup_start
        )
        bus_cleanup = self.spi[bus_cleanup_start:bus_cleanup_end]
        bus_close_start = self.spi.index("JSValue js_spi_bus_close(")
        bus_close_end = self.spi.index(
            "\nJSValue js_spi_bus_status(", bus_close_start
        )
        bus_close = self.spi[bus_close_start:bus_close_end]
        device_close_start = self.spi.index("JSValue js_spi_device_close(")
        device_close_end = self.spi.index(
            "\nJSValue js_spi_device_status(", device_close_start
        )
        device_close = self.spi[device_close_start:device_close_end]

        self.assertIn(
            "esp32_mquickjs_spi_device_resources_deinit(", device_cleanup
        )
        self.assertNotIn("spi_bus_remove_device", device_cleanup)
        self.assertLess(
            device_cleanup.index("if (err != ESP_OK)"),
            device_cleanup.index("parent->open_devices--"),
        )
        self.assertIn("err = spi_cleanup_device_slot(device_slot)", bus_cleanup)
        self.assertIn("if (err != ESP_OK)", bus_cleanup)
        self.assertIn("esp32_mquickjs_spi_bus_resources_deinit(", bus_cleanup)
        self.assertNotIn("spi_bus_free", bus_cleanup)
        self.assertLess(
            bus_cleanup.rindex("if (err != ESP_OK)"),
            bus_cleanup.index("spi_free_staging_workspace(slot)"),
        )
        for close in (bus_close, device_close):
            self.assertIn("err = spi_cleanup_", close)
            self.assertLess(
                close.index("if (err != ESP_OK)"),
                close.index("generation = 0"),
            )
        self.assertIn("resources->handle = NULL", resources)
        self.assertIn("resources->initialized = false", resources)
        self.assertIn(
            "src/modules/spi/esp32_mquickjs_spi_native_resources.c", cmake
        )

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
        self.assertIn("state->completion_queue.in_flight > 0", self.spi)
        self.assertIn(
            "esp32_mquickjs_dma_completion_queue_note_returned", step
        )
        self.assertIn(
            "esp32_mquickjs_dma_completion_queue_releasable", self.spi
        )
        self.assertIn("ESP32_MQUICKJS_CANCEL_REQUESTED", self.spi)

    def test_spi_reaps_completion_before_declaring_progress_timeout(self):
        step = self.spi.split("static void spi_future_step(", 2)[-1].split(
            "static bool spi_future_start", 1
        )[0]

        self.assertIn("esp32_mquickjs_dma_progress_timed_out", step)
        self.assertLess(
            step.index("spi_device_get_trans_result"),
            step.index("esp32_mquickjs_dma_progress_timed_out"),
            "a completion already queued by ESP-IDF must win over a late scheduler poll",
        )
        self.assertIn("made_progress", step)

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
