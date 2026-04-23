set(srcs
    "app_main.c"
)

set(requires
    esp32_mquickjs
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES ${requires}
)

if(CONFIG_ESP32QJS_ENABLE_REPL)
    idf_component_optional_requires(PRIVATE esp32qjs_interactive)
endif()

set(ESP32QJS_FLASH_DATA_DIR "${CMAKE_SOURCE_DIR}/flash_data" CACHE PATH
    "Directory used to build the LittleFS storage image")

littlefs_create_partition_image(storage "${ESP32QJS_FLASH_DATA_DIR}" FLASH_IN_PROJECT)
