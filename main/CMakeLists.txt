idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp32qjs_runtime
)

set(_esp32qjs_flash_data_dir
    "${CMAKE_BINARY_DIR}/esp32qjs_flash_data/context")
file(REMOVE_RECURSE "${_esp32qjs_flash_data_dir}")
file(MAKE_DIRECTORY "${_esp32qjs_flash_data_dir}")
file(COPY "${ESP32QJS_BUILD_CONTEXT_DIR}/flash_data/"
     DESTINATION "${_esp32qjs_flash_data_dir}")

set(_esp32qjs_precompile_script
    "${CMAKE_CURRENT_LIST_DIR}/../scripts/precompile_startup.py")
idf_build_get_property(_esp32qjs_python PYTHON)
execute_process(
    COMMAND
        "${_esp32qjs_python}"
        "${_esp32qjs_precompile_script}"
        --manifest "${ESP32QJS_BUILD_CONTEXT_DIR}/precompile.json"
        --source-root "${ESP32QJS_BUILD_CONTEXT_DIR}/flash_data"
        --staging-root "${_esp32qjs_flash_data_dir}"
        --build-dir "${CMAKE_BINARY_DIR}/esp32qjs_startup_precompile/context"
    RESULT_VARIABLE _esp32qjs_precompile_result
    OUTPUT_VARIABLE _esp32qjs_precompile_stdout
    ERROR_VARIABLE _esp32qjs_precompile_stderr
)
if(NOT _esp32qjs_precompile_result EQUAL 0)
    message(FATAL_ERROR
        "Build Context startup precompile failed:\n"
        "${_esp32qjs_precompile_stdout}${_esp32qjs_precompile_stderr}")
endif()
string(STRIP "${_esp32qjs_precompile_stdout}" _esp32qjs_precompile_summary)
message(STATUS "${_esp32qjs_precompile_summary}")

message(STATUS
    "ESP32QJS Build Context LittleFS source: ${_esp32qjs_flash_data_dir}")
littlefs_create_partition_image(
    storage "${_esp32qjs_flash_data_dir}" FLASH_IN_PROJECT)

if(CONFIG_ESP32QJS_SECONDARY_LITTLEFS)
    set(_esp32qjs_empty_secondary_fs_dir
        "${CMAKE_BINARY_DIR}/esp32qjs_secondary_fs_empty")
    file(REMOVE_RECURSE "${_esp32qjs_empty_secondary_fs_dir}")
    file(MAKE_DIRECTORY "${_esp32qjs_empty_secondary_fs_dir}")
    littlefs_create_partition_image(
        ${CONFIG_ESP32QJS_SECONDARY_LITTLEFS_PARTITION_LABEL}
        "${_esp32qjs_empty_secondary_fs_dir}"
        FLASH_IN_PROJECT
    )
endif()
