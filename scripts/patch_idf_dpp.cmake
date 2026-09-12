# Fixed SDK DPP inputs/configuration: no edits to the shared ESP-IDF checkout.
if(NOT TARGET idf::wpa_supplicant OR NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI OR NOT CONFIG_ESP_WIFI_DPP_SUPPORT)
    return()
endif()
idf_component_get_property(_esp32qjs_dpp_lib wpa_supplicant COMPONENT_LIB)
idf_component_get_property(_esp32qjs_dpp_dir wpa_supplicant COMPONENT_DIR)
idf_build_get_property(_esp32qjs_dpp_python PYTHON)
set(_esp32qjs_dpp_script "${CMAKE_CURRENT_LIST_DIR}/patch_idf_dpp.py")
set(_esp32qjs_dpp_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/dpp")
set(_esp32qjs_dpp_parts "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/src/modules/wifi_dpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_esp32qjs_dpp_script}" "${_esp32qjs_dpp_parts}/esp32_mquickjs_dpp_bootstrap.inc"
    "${_esp32qjs_dpp_parts}/esp32_mquickjs_dpp_bootstrap_common.inc"
    "${_esp32qjs_dpp_parts}/esp32_mquickjs_dpp_config.inc"
    "${_esp32qjs_dpp_parts}/esp32_mquickjs_dpp_async.inc"
    "${_esp32qjs_dpp_parts}/esp32_mquickjs_dpp_result.inc"
    "${_esp32qjs_dpp_parts}/esp32_mquickjs_dpp_roc.inc"
    "${_esp32qjs_dpp_parts}/esp32_mquickjs_dpp_tx.inc"
    "${_esp32qjs_dpp_parts}/esp32_mquickjs_dpp_deinit.inc"
    "${_esp32qjs_dpp_parts}/esp32_mquickjs_dpp_public_deinit.inc"
    "${_esp32qjs_dpp_parts}/esp32_mquickjs_dpp_commands.inc"
    "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/internal/esp32_mquickjs_wifi_dpp_result.h")
target_include_directories(${_esp32qjs_dpp_lib} PRIVATE
    "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/internal")
foreach(_esp32qjs_dpp_input "esp_supplicant/src/esp_dpp.c" "esp_supplicant/src/esp_dpp_i.h"
        "esp_supplicant/include/esp_dpp.h" "src/common/dpp.c" "src/common/dpp.h"
        "src/utils/common.c" "port/eloop.c" "src/utils/eloop.h" "src/common/dpp_crypto.c"
        "../../examples/wifi/wifi_easy_connect/dpp-enrollee/main/dpp_enrollee_main.c"
        "../esp_wifi/include/esp_wifi_types_generic.h")
    get_filename_component(_esp32qjs_dpp_dependency "${_esp32qjs_dpp_dir}/${_esp32qjs_dpp_input}" REALPATH)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_esp32qjs_dpp_dependency}")
endforeach()
execute_process(COMMAND "${_esp32qjs_dpp_python}" "${_esp32qjs_dpp_script}"
    --component "${_esp32qjs_dpp_dir}" --output-dir "${_esp32qjs_dpp_output}"
    RESULT_VARIABLE _esp32qjs_dpp_result ERROR_VARIABLE _esp32qjs_dpp_error)
if(NOT _esp32qjs_dpp_result EQUAL 0)
    message(FATAL_ERROR "ESP32QJS DPP fix failed: ${_esp32qjs_dpp_error}")
endif()
get_target_property(_esp32qjs_dpp_sources ${_esp32qjs_dpp_lib} SOURCES)
set(_esp32qjs_dpp_new_sources "")
set(_esp32qjs_dpp_count 0)
foreach(_esp32qjs_dpp_entry IN LISTS _esp32qjs_dpp_sources)
    get_filename_component(_esp32qjs_dpp_absolute "${_esp32qjs_dpp_entry}" ABSOLUTE BASE_DIR "${_esp32qjs_dpp_dir}")
    if(_esp32qjs_dpp_absolute STREQUAL "${_esp32qjs_dpp_dir}/esp_supplicant/src/esp_dpp.c"
       OR _esp32qjs_dpp_absolute STREQUAL "${_esp32qjs_dpp_dir}/src/common/dpp.c")
        get_filename_component(_esp32qjs_dpp_name "${_esp32qjs_dpp_absolute}" NAME)
        list(APPEND _esp32qjs_dpp_new_sources "${_esp32qjs_dpp_output}/${_esp32qjs_dpp_name}")
        math(EXPR _esp32qjs_dpp_count "${_esp32qjs_dpp_count} + 1")
    else()
        list(APPEND _esp32qjs_dpp_new_sources "${_esp32qjs_dpp_entry}")
    endif()
endforeach()
if(NOT _esp32qjs_dpp_count EQUAL 2)
    message(FATAL_ERROR "Expected exactly two SDK DPP source replacements, found ${_esp32qjs_dpp_count}")
endif()
set_property(TARGET ${_esp32qjs_dpp_lib} PROPERTY SOURCES "${_esp32qjs_dpp_new_sources}")
