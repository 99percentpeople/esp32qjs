set(srcs
    "app_main.c"
    "runtime.c"
)

set(requires
    esp32_mquickjs
    esp_driver_usb_serial_jtag
)

if(CONFIG_ESP32QJS_ENABLE_REPL)
    list(APPEND srcs
        "repl.c"
        "repl_input.c"
    )
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES ${requires}
)

littlefs_create_partition_image(storage ../flash_data FLASH_IN_PROJECT)
