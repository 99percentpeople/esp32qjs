# Apply fixed-SDK RRM corrections in a build-local translation unit only.
if(NOT TARGET idf::wpa_supplicant OR NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI OR NOT CONFIG_ESP_WIFI_RRM_SUPPORT)
    return()
endif()

idf_component_get_property(_esp32qjs_rrm_lib wpa_supplicant COMPONENT_LIB)
idf_component_get_property(_esp32qjs_rrm_dir wpa_supplicant COMPONENT_DIR)
idf_build_get_property(_esp32qjs_rrm_python PYTHON)
set(_esp32qjs_rrm_source "${_esp32qjs_rrm_dir}/src/common/rrm.c")
set(_esp32qjs_rrm_patch_script "${CMAKE_CURRENT_LIST_DIR}/patch_idf_rrm.py")
set(_esp32qjs_rrm_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/rrm.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_esp32qjs_rrm_source}" "${_esp32qjs_rrm_patch_script}"
    "${_esp32qjs_rrm_dir}/esp_supplicant/src/esp_common.c"
    "${_esp32qjs_rrm_dir}/src/common/wpa_supplicant_i.h"
    "${_esp32qjs_rrm_dir}/port/eloop.c")
execute_process(
    COMMAND "${_esp32qjs_rrm_python}" "${_esp32qjs_rrm_patch_script}"
        --component "${_esp32qjs_rrm_dir}" --output "${_esp32qjs_rrm_output}"
    RESULT_VARIABLE _esp32qjs_rrm_patch_result
    OUTPUT_VARIABLE _esp32qjs_rrm_patch_message
    ERROR_VARIABLE _esp32qjs_rrm_patch_error)
if(NOT _esp32qjs_rrm_patch_result EQUAL 0)
    message(FATAL_ERROR "ESP32QJS RRM fix failed: ${_esp32qjs_rrm_patch_error}")
endif()

get_target_property(_esp32qjs_rrm_sources ${_esp32qjs_rrm_lib} SOURCES)
set(_esp32qjs_rrm_replacements 0)
set(_esp32qjs_rrm_new_sources "")
foreach(_esp32qjs_rrm_entry IN LISTS _esp32qjs_rrm_sources)
    get_filename_component(_esp32qjs_rrm_absolute "${_esp32qjs_rrm_entry}"
        ABSOLUTE BASE_DIR "${_esp32qjs_rrm_dir}")
    if(_esp32qjs_rrm_absolute STREQUAL _esp32qjs_rrm_source)
        list(APPEND _esp32qjs_rrm_new_sources "${_esp32qjs_rrm_output}")
        math(EXPR _esp32qjs_rrm_replacements "${_esp32qjs_rrm_replacements} + 1")
    else()
        list(APPEND _esp32qjs_rrm_new_sources "${_esp32qjs_rrm_entry}")
    endif()
endforeach()
if(NOT _esp32qjs_rrm_replacements EQUAL 1)
    message(FATAL_ERROR "Expected exactly one rrm.c in the SDK component")
endif()
set_property(TARGET ${_esp32qjs_rrm_lib} PROPERTY SOURCES "${_esp32qjs_rrm_new_sources}")
string(STRIP "${_esp32qjs_rrm_patch_message}" _esp32qjs_rrm_patch_message)
message(STATUS "${_esp32qjs_rrm_patch_message}")
