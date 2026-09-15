if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI OR NOT CONFIG_ESP_WIFI_NAN_USD_ENABLE)
    return()
endif()
if(NOT TARGET idf::wpa_supplicant OR CONFIG_ESP_HOST_WIFI_ENABLED)
    message(FATAL_ERROR "NAN USD requires the reviewed local supplicant")
endif()
idf_component_get_property(_usd_lib wpa_supplicant COMPONENT_LIB)
idf_component_get_property(_usd_dir wpa_supplicant COMPONENT_DIR)
idf_build_get_property(_usd_python PYTHON)
set(_usd_script "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/nan_usd.py")
set(_usd_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/nan_usd")
set(_usd_framework "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_usd_script}" "${_usd_framework}/internal/esp32_mquickjs_wifi_nan_usd_sdk.h"
    "${_usd_framework}/src/modules/wifi_nan/esp32_mquickjs_wifi_nan_usd_state.inc"
    "${_usd_framework}/src/modules/wifi_nan/esp32_mquickjs_wifi_nan_usd_commands.inc"
    "${_usd_framework}/src/modules/wifi_nan/esp32_mquickjs_wifi_nan_usd_transport.inc"
    "${_usd_framework}/src/modules/wifi_nan/esp32_mquickjs_wifi_nan_usd_lifecycle.inc")
foreach(_usd_input IN ITEMS esp_supplicant/src/esp_nan_usd.c src/common/nan_de.c
        src/common/nan_de.h port/eloop.c CMakeLists.txt)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_usd_dir}/${_usd_input}")
endforeach()
execute_process(COMMAND "${_usd_python}" "${CMAKE_SOURCE_DIR}/scripts/sdk_patch.py" nan_usd --component "${_usd_dir}" --output-dir "${_usd_output}"
    RESULT_VARIABLE _usd_result ERROR_VARIABLE _usd_error)
if(NOT _usd_result EQUAL 0)
    message(FATAL_ERROR "NAN USD preparation failed: ${_usd_error}")
endif()
target_include_directories(${_usd_lib} PRIVATE "${_usd_framework}/internal"
    "${_usd_framework}/src/modules/wifi_nan")
get_target_property(_usd_sources ${_usd_lib} SOURCES)
set(_usd_prepared "")
set(_usd_count 0)
foreach(_usd_source IN LISTS _usd_sources)
    get_filename_component(_usd_absolute "${_usd_source}" ABSOLUTE BASE_DIR "${_usd_dir}")
    if(_usd_absolute STREQUAL "${_usd_dir}/esp_supplicant/src/esp_nan_usd.c" OR
       _usd_absolute STREQUAL "${_usd_dir}/src/common/nan_de.c")
        get_filename_component(_usd_name "${_usd_absolute}" NAME)
        list(APPEND _usd_prepared "${_usd_output}/${_usd_name}")
        math(EXPR _usd_count "${_usd_count} + 1")
    else()
        list(APPEND _usd_prepared "${_usd_source}")
    endif()
endforeach()
if(NOT _usd_count EQUAL 2)
    message(FATAL_ERROR "Expected exactly two NAN USD source replacements, found ${_usd_count}")
endif()
set_property(TARGET ${_usd_lib} PROPERTY SOURCES "${_usd_prepared}")
