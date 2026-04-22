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
        "repl_line.c"
        "repl.c"
        "repl_input.c"
    )
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES ${requires}
)

set(ESP32QJS_FLASH_DATA_DIR "${CMAKE_SOURCE_DIR}/flash_data" CACHE PATH
    "Directory used to build the LittleFS storage image")

littlefs_create_partition_image(storage "${ESP32QJS_FLASH_DATA_DIR}" FLASH_IN_PROJECT)
