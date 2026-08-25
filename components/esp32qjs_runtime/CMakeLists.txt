idf_component_register(
    SRCS "src/esp32qjs_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp32_mquickjs
        esp_system
        esp_timer
        nvs_flash
)

if(CONFIG_ESP32QJS_ENABLE_REPL)
    idf_component_optional_requires(PRIVATE esp32qjs_interactive)
endif()
