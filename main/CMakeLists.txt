set(srcs
    "app_main.c"
)

set(requires
    esp32qjs_runtime
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES ${requires}
)

if(NOT "${ESP32QJS_FLASH_DATA_DIR}" STREQUAL "")
    if(NOT IS_DIRECTORY "${ESP32QJS_FLASH_DATA_DIR}")
        message(FATAL_ERROR "ESP32QJS_FLASH_DATA_DIR does not exist: ${ESP32QJS_FLASH_DATA_DIR}")
    endif()
    set(_esp32qjs_effective_flash_data_dir "${ESP32QJS_FLASH_DATA_DIR}")
else()
    if(NOT IS_DIRECTORY "${ESP32QJS_SHARED_FLASH_DATA_DIR}")
        message(FATAL_ERROR
            "ESP32QJS_SHARED_FLASH_DATA_DIR does not exist: ${ESP32QJS_SHARED_FLASH_DATA_DIR}")
    endif()
    if(NOT IS_DIRECTORY "${ESP32QJS_APP_FLASH_DATA_DIR}")
        message(FATAL_ERROR
            "ESP32QJS_APP_FLASH_DATA_DIR does not exist: ${ESP32QJS_APP_FLASH_DATA_DIR}")
    endif()

    set(_esp32qjs_effective_flash_data_dir
        "${CMAKE_BINARY_DIR}/esp32qjs_flash_data/${ESP32QJS_APP}")
    file(REMOVE_RECURSE "${_esp32qjs_effective_flash_data_dir}")
    file(MAKE_DIRECTORY "${_esp32qjs_effective_flash_data_dir}")
    file(COPY "${ESP32QJS_SHARED_FLASH_DATA_DIR}/"
         DESTINATION "${_esp32qjs_effective_flash_data_dir}")
    file(COPY "${ESP32QJS_APP_FLASH_DATA_DIR}/"
         DESTINATION "${_esp32qjs_effective_flash_data_dir}")
endif()

message(STATUS
    "ESP32QJS app '${ESP32QJS_APP}' LittleFS source: ${_esp32qjs_effective_flash_data_dir}")
littlefs_create_partition_image(storage "${_esp32qjs_effective_flash_data_dir}" FLASH_IN_PROJECT)
