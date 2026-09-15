# Enforce immutable quotas for both the repository helper and direct IDF builds.
idf_build_get_property(_esp32qjs_budget_python PYTHON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/scripts/validate_wireless_budget.py"
    "${CMAKE_SOURCE_DIR}/scripts/build_tools/wireless_budget.py")
execute_process(
    COMMAND "${_esp32qjs_budget_python}"
        "${CMAKE_SOURCE_DIR}/scripts/validate_wireless_budget.py"
        --build-context "${ESP32QJS_BUILD_CONTEXT_DIR}"
        --sdkconfig "${CMAKE_BINARY_DIR}/config/sdkconfig.json"
    RESULT_VARIABLE _esp32qjs_budget_result
    OUTPUT_VARIABLE _esp32qjs_budget_output
    ERROR_VARIABLE _esp32qjs_budget_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _esp32qjs_budget_result EQUAL 0)
    message(FATAL_ERROR "${_esp32qjs_budget_error}")
endif()
message(STATUS "ESP32QJS wireless admission: ${_esp32qjs_budget_output}")
