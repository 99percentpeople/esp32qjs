if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI)
    return()
endif()
idf_build_get_property(_esp32qjs_coverage_python PYTHON)
idf_build_get_property(_esp32qjs_coverage_idf IDF_PATH)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/scripts/generate_idf_wifi_api_map.py"
    "${CMAKE_SOURCE_DIR}/scripts/inspect_idf_wifi_inputs.py"
    "${CMAKE_SOURCE_DIR}/scripts/codegen/idf_wifi_api_map.py"
    "${CMAKE_SOURCE_DIR}/scripts/codegen/idf_wifi_inputs.py"
    "${CMAKE_SOURCE_DIR}/scripts/tool_paths.py"
    "${CMAKE_SOURCE_DIR}/docs/idf-wifi-api-map.json"
    "${CMAKE_SOURCE_DIR}/docs/idf-wifi-api-inventory.json"
    "${CMAKE_SOURCE_DIR}/api-manifest.json"
    "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/internal/esp32_mquickjs_wifi_coverage.inc")
execute_process(
    COMMAND "${_esp32qjs_coverage_python}"
        "${CMAKE_SOURCE_DIR}/scripts/generate_idf_wifi_api_map.py"
        --check-headers "${_esp32qjs_coverage_idf}"
    RESULT_VARIABLE _esp32qjs_coverage_result
    OUTPUT_VARIABLE _esp32qjs_coverage_output
    ERROR_VARIABLE _esp32qjs_coverage_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _esp32qjs_coverage_result EQUAL 0)
    message(FATAL_ERROR "${_esp32qjs_coverage_error}")
endif()
message(STATUS "ESP32QJS Wi-Fi diagnostics: ${_esp32qjs_coverage_output}")
