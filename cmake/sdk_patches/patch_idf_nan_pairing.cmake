# Native pairing is part of the pinned IDF. Do not add a second Wi-Fi stack.
if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI OR NOT CONFIG_ESP_WIFI_NAN_PAIRING)
    return()
endif()
if(NOT CONFIG_ESP_WIFI_NAN_SYNC_ENABLE OR NOT CONFIG_ESP_WIFI_NAN_SECURITY OR
   NOT TARGET idf::esp_wifi OR NOT TARGET idf::wpa_supplicant OR CONFIG_ESP_HOST_WIFI_ENABLED)
    message(FATAL_ERROR "NAN pairing requires the reviewed local Sync/security stack")
endif()
idf_component_get_property(_pair_wifi_dir esp_wifi COMPONENT_DIR)
get_filename_component(_pair_components "${_pair_wifi_dir}" DIRECTORY)
idf_build_get_property(_pair_python PYTHON)
set(_pair_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/nan_pairing")
set(_pair_script "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wifi/nan_pairing.py")
set(_pair_framework "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_pair_script}"
    "${_pair_framework}/internal/esp32_mquickjs_wifi_nan_pasn_sdk.h"
    "${_pair_framework}/src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pasn_state.inc"
    "${_pair_framework}/src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pasn_commands.inc"
    "${_pair_framework}/src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pairing_pending.inc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_pair_framework}/src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pairing_binding.inc"
    "${_pair_framework}/src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pairing_start.inc")
foreach(_pair_input IN ITEMS esp_wifi/wifi_apps/nan_app/src/nan_pairing.c
    esp_wifi/wifi_apps/include/apps_private/wifi_apps_private.h
    wpa_supplicant/esp_supplicant/src/esp_nan_supplicant.c
    wpa_supplicant/esp_supplicant/src/esp_nan_supp_i.h
    wpa_supplicant/esp_supplicant/include/esp_private/esp_supp_nan.h
    wpa_supplicant/port/eloop.c wpa_supplicant/CMakeLists.txt)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_pair_components}/${_pair_input}")
endforeach()
execute_process(COMMAND "${_pair_python}" "${CMAKE_SOURCE_DIR}/scripts/sdk_patch.py" nan_pairing --components "${_pair_components}" --output-dir "${_pair_output}"
    RESULT_VARIABLE _pair_result ERROR_VARIABLE _pair_error)
if(NOT _pair_result EQUAL 0)
    message(FATAL_ERROR "NAN pairing preparation failed: ${_pair_error}")
endif()
foreach(_pair_component IN ITEMS esp_wifi wpa_supplicant)
    idf_component_get_property(_pair_lib ${_pair_component} COMPONENT_LIB)
    idf_component_get_property(_pair_dir ${_pair_component} COMPONENT_DIR)
    if(_pair_component STREQUAL "esp_wifi")
        set(_pair_original "${_pair_dir}/wifi_apps/nan_app/src/nan_pairing.c")
    else()
        set(_pair_original "${_pair_dir}/esp_supplicant/src/esp_nan_supplicant.c")
    endif()
    get_filename_component(_pair_name "${_pair_original}" NAME)
    get_target_property(_pair_sources ${_pair_lib} SOURCES)
    set(_pair_prepared "")
    set(_pair_count 0)
    foreach(_pair_source IN LISTS _pair_sources)
        get_filename_component(_pair_absolute "${_pair_source}" ABSOLUTE BASE_DIR "${_pair_dir}")
        if(_pair_absolute STREQUAL _pair_original)
            # IDF lists the supplicant source in both common and pairing lists.
            if(_pair_count EQUAL 0)
                list(APPEND _pair_prepared "${_pair_output}/${_pair_name}")
            endif()
            math(EXPR _pair_count "${_pair_count} + 1")
        else()
            list(APPEND _pair_prepared "${_pair_source}")
        endif()
    endforeach()
    if(_pair_count LESS 1 OR _pair_count GREATER 2)
        message(FATAL_ERROR "Unexpected NAN pairing source count for ${_pair_component}: ${_pair_count}")
    endif()
    set_property(TARGET ${_pair_lib} PROPERTY SOURCES "${_pair_prepared}")
    target_include_directories(${_pair_lib} PRIVATE "${_pair_framework}/internal"
        "${_pair_framework}/src/modules/wifi_nan")
endforeach()
