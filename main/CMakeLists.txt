idf_component_register(
    SRCS
        "app_main.c"
        "repl.c"
        "repl_input.c"
    INCLUDE_DIRS "."
    REQUIRES esp32_mquickjs esp_driver_usb_serial_jtag
)

littlefs_create_partition_image(storage ../flash_data FLASH_IN_PROJECT)
