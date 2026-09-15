# Keep NAN adaptation in this immutable build; never edit the shared SDK.
if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI OR
   NOT (CONFIG_ESP_WIFI_NAN_SYNC_ENABLE OR CONFIG_ESP_WIFI_NAN_USD_ENABLE))
    return()
endif()
if(NOT TARGET idf::esp_wifi OR CONFIG_ESP_HOST_WIFI_ENABLED)
    message(FATAL_ERROR "NAN requires the reviewed local ESP-IDF implementation")
endif()
idf_component_get_property(_nan_lib esp_wifi COMPONENT_LIB)
idf_component_get_property(_nan_dir esp_wifi COMPONENT_DIR)
idf_build_get_property(_nan_python PYTHON)
set(_nan_script "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wifi/nan.py")
set(_nan_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/nan/nan_app.c")
set(_nan_framework "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_nan_script}"
    "${_nan_framework}/internal/esp32_mquickjs_wifi_nan_sdk.h"
    "${_nan_framework}/internal/esp32_mquickjs_wifi_nan_query.h"
    "${_nan_framework}/internal/esp32_mquickjs_wifi_nan_ndp.h"
    "${_nan_framework}/internal/esp32_mquickjs_wifi_nan_timer.h"
    "${_nan_framework}/internal/esp32_mquickjs_wifi_nan_usd_sdk.h"
    "${_nan_framework}/src/modules/wifi_nan/esp32_mquickjs_wifi_nan_sdk.inc"
    "${_nan_framework}/src/modules/wifi_nan/esp32_mquickjs_wifi_nan_query_sdk.inc"
    "${_nan_framework}/src/modules/wifi_nan/esp32_mquickjs_wifi_nan_sdk_ndp.inc")
foreach(_nan_input IN ITEMS wifi_apps/nan_app/src/nan_app.c wifi_apps/nan_app/src/nan_security.c wifi_apps/nan_app/src/nan_i.h
        wifi_apps/nan_app/include/esp_nan.h include/esp_wifi_types_generic.h
        src/wifi_default.c CMakeLists.txt)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_nan_dir}/${_nan_input}")
endforeach()
execute_process(COMMAND "${_nan_python}" "${CMAKE_SOURCE_DIR}/scripts/sdk_patch.py" nan --component "${_nan_dir}" --output "${_nan_output}"
    RESULT_VARIABLE _nan_result OUTPUT_VARIABLE _nan_message ERROR_VARIABLE _nan_error)
if(NOT _nan_result EQUAL 0)
    message(FATAL_ERROR "ESP32QJS NAN preparation failed: ${_nan_error}")
endif()
get_target_property(_nan_sources ${_nan_lib} SOURCES)
set(_nan_new_sources "")
set(_nan_count 0)
set(_nan_security_count 0)
foreach(_nan_source IN LISTS _nan_sources)
    get_filename_component(_nan_absolute "${_nan_source}" ABSOLUTE BASE_DIR "${_nan_dir}")
    if(_nan_absolute STREQUAL "${_nan_dir}/wifi_apps/nan_app/src/nan_app.c")
        list(APPEND _nan_new_sources "${_nan_output}")
        math(EXPR _nan_count "${_nan_count} + 1")
    elseif(_nan_absolute STREQUAL "${_nan_dir}/wifi_apps/nan_app/src/nan_security.c")
        list(APPEND _nan_new_sources "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/nan/nan_security.c")
        math(EXPR _nan_security_count "${_nan_security_count} + 1")
    else()
        list(APPEND _nan_new_sources "${_nan_source}")
    endif()
endforeach()
if(NOT _nan_count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one reviewed NAN application source")
endif()
if(CONFIG_ESP_WIFI_NAN_SECURITY AND NOT _nan_security_count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one reviewed NAN security source")
endif()
set_property(TARGET ${_nan_lib} PROPERTY SOURCES "${_nan_new_sources}")
target_include_directories(${_nan_lib} PRIVATE "${_nan_framework}/internal"
    "${_nan_framework}/src/modules/wifi_nan" "${_nan_dir}/wifi_apps/nan_app/src")
string(STRIP "${_nan_message}" _nan_message)
message(STATUS "${_nan_message}")
