import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class MemoryManagerArchitectureTests(unittest.TestCase):
    def test_manager_uses_partition_reserves_and_safe_point_migration(self):
        source = (MQUICKJS / "src/core/esp32_mquickjs_memory.c").read_text(
            encoding="utf-8"
        )
        runtime = (MQUICKJS / "src/core/esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA", source)
        self.assertIn("dma.largest_free_block", source)
        self.assertIn("internal.total_free_bytes", source)
        self.assertIn("MEMORY_MAX_ACTIONS_PER_PASS", source)
        self.assertIn("block->borrows == 0", source)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", source)
        self.assertIn("memory_migrate_one", source)
        self.assertIn("memory_evict_one", source)
        self.assertIn("memory_class_counts_as_pinned", source)
        self.assertIn("return !memory_class_is_movable(memory_class);", source)
        self.assertIn("esp32_mquickjs_memory_maintain();", runtime)
        self.assertIn("if (!esp32_mquickjs_execution_active(runtime))", runtime)
        self.assertIn("JS_FreeContext(ctx);\n        esp32_mquickjs_memory_release_generation();", runtime)
        self.assertIn("void esp32_mquickjs_memory_release_generation(void)", source)
        self.assertNotIn("esp32_mquickjs_memory_maintain();", source.split(
            "void esp32_mquickjs_memory_maintain(void)", 1
        )[0])
        dma_preflight = source.split(
            "bool esp32_mquickjs_memory_prepare_internal_dma", 1
        )[1].split("void esp32_mquickjs_memory_get_status", 1)[0]
        self.assertNotIn("esp32_mquickjs_memory_maintain();", dma_preflight)
        self.assertNotIn("M5", source)
        self.assertNotIn("StickS3", source)

    def test_large_framework_payloads_use_the_shared_policy(self):
        payload_sources = [
            "src/core/esp32_mquickjs_stream.c",
            "src/modules/fs/esp32_mquickjs_fs.c",
            "src/modules/http/esp32_mquickjs_http.c",
            "src/modules/socket/esp32_mquickjs_socket.c",
            "src/modules/usb_serial/esp32_mquickjs_usb_serial.c",
            "src/modules/bitmap/esp32_mquickjs_bitmap.c",
        ]

        for relative in payload_sources:
            with self.subTest(source=relative):
                source = (MQUICKJS / relative).read_text(encoding="utf-8")
                self.assertIn("esp32_mquickjs_memory_payload_", source)

        i2s = (MQUICKJS / "src/modules/i2s/esp32_mquickjs_i2s.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("esp32_mquickjs_memory_prepare_internal_dma", i2s)
        self.assertEqual(i2s.count("i2s_new_channel("), 1)

        spi = (MQUICKJS / "src/modules/spi/esp32_mquickjs_spi.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL", spi)
        self.assertIn("SPI_TRANS_DMA_USE_PSRAM", spi)

    def test_external_dma_class_falls_back_once_to_reserved_internal_dma(self):
        declarations = (
            MQUICKJS / "internal/esp32_mquickjs_memory.h"
        ).read_text(encoding="utf-8")
        source = (MQUICKJS / "src/core/esp32_mquickjs_memory.c").read_text(
            encoding="utf-8"
        )
        docs = (ROOT / "docs/sys-management-api.md").read_text(
            encoding="utf-8"
        )
        allocation_case = source.split(
            "case ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL:", 1
        )[1].split(
            "case ESP32_MQUICKJS_MEMORY_EXTERNAL:", 1
        )[0]
        realloc_policy = source.split(
            "static uint32_t memory_realloc_caps", 1
        )[1].split(
            "void *esp32_mquickjs_memory_payload_realloc", 1
        )[0]
        realloc_body = source.split(
            "void *esp32_mquickjs_memory_payload_realloc", 1
        )[1].split(
            "static esp32_mquickjs_memory_block_t", 1
        )[0]

        self.assertIn("ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL,", declarations)
        self.assertNotIn("DMA_EXTERNAL_IF_SUPPORTED", declarations)
        self.assertIn("if (has_external_dma)", allocation_case)
        self.assertIn(
            "data == NULL && memory_internal_dma_can_fit(size)",
            allocation_case,
        )
        self.assertNotIn("data == NULL && !has_external_dma", allocation_case)
        self.assertIn("memory_has_external_dma()", realloc_policy)
        self.assertIn("external_dma_class && !retry_internal_dma", realloc_body)
        self.assertIn("retry_internal_dma", realloc_body)
        self.assertIn("memory_internal_dma_can_fit(size)", realloc_body)
        self.assertEqual(realloc_body.count("heap_caps_realloc("), 2)
        self.assertIn('`DMA_EXTERNAL` class means "prefer external DMA"', docs)
        self.assertIn("records one\nallocation failure", docs)
        self.assertIn(
            "`DMA_EXTERNAL` blocks that fell back to internal RAM",
            docs,
        )

    def test_status_and_movable_display_buffers_are_exposed(self):
        stdlib = (MQUICKJS / "src/core/mqjs_stdlib_esp32.c").read_text(
            encoding="utf-8"
        )
        sys_source = (MQUICKJS / "src/core/esp32_mquickjs_sys.c").read_text(
            encoding="utf-8"
        )
        commands = (
            MQUICKJS
            / "src/modules/bitmap/esp32_mquickjs_display_command_buffer.c"
        ).read_text(encoding="utf-8")
        fonts = (MQUICKJS / "src/modules/bitmap/esp32_mquickjs_bitmap_font.c").read_text(
            encoding="utf-8"
        )

        self.assertIn('JS_CGETSET_DEF("manager", js_sys_memory_manager', stdlib)
        self.assertIn('"dmaLargestReserveBytes"', sys_source)
        self.assertIn('"migrationCount"', sys_source)
        self.assertIn("esp32_mquickjs_memory_block_alloc", commands)
        self.assertIn("esp32_mquickjs_memory_block_borrow", commands)
        self.assertIn("esp32_mquickjs_memory_block_release", commands)
        self.assertIn("esp32_mquickjs_memory_block_borrow", fonts)
        self.assertIn("esp32_mquickjs_memory_block_release", fonts)


if __name__ == "__main__":
    unittest.main()
