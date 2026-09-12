# Keep fixed SDK EAP credential cleanup changes in this build directory.
if(NOT TARGET idf::wpa_supplicant OR NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI OR NOT CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT)
    return()
endif()
idf_component_get_property(_esp32qjs_eap_lib wpa_supplicant COMPONENT_LIB)
idf_component_get_property(_esp32qjs_eap_dir wpa_supplicant COMPONENT_DIR)
idf_build_get_property(_esp32qjs_eap_python PYTHON)
set(_esp32qjs_eap_script "${CMAKE_CURRENT_LIST_DIR}/patch_idf_eap_control.py")
set(_esp32qjs_eap_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/eap_secrets")
set(_esp32qjs_eap_inputs
    "esp_supplicant/src/esp_eap_client.c" "src/eap_peer/eap.c"
    "esp_supplicant/include/esp_eap_client.h" "src/eap_peer/eap_config.h"
    "src/utils/common.c" "port/os_xtensa.c" "port/include/os.h" "CMakeLists.txt"
    "port/eloop.c" "esp_supplicant/src/esp_wifi_driver.h" "src/eap_peer/eap_i.h"
    "esp_supplicant/src/esp_wpa_main.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_esp32qjs_eap_script}"
    "${CMAKE_CURRENT_LIST_DIR}/patch_idf_eap_secrets.py"
    "${CMAKE_CURRENT_LIST_DIR}/patch_idf_eap_lifecycle.py")
foreach(_esp32qjs_eap_input IN LISTS _esp32qjs_eap_inputs)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_esp32qjs_eap_dir}/${_esp32qjs_eap_input}")
endforeach()
execute_process(
    COMMAND "${_esp32qjs_eap_python}" "${_esp32qjs_eap_script}"
        --component "${_esp32qjs_eap_dir}" --output-dir "${_esp32qjs_eap_output}"
    RESULT_VARIABLE _esp32qjs_eap_result
    OUTPUT_VARIABLE _esp32qjs_eap_message
    ERROR_VARIABLE _esp32qjs_eap_error)
if(NOT _esp32qjs_eap_result EQUAL 0)
    message(FATAL_ERROR "ESP32QJS EAP cleanup fix failed: ${_esp32qjs_eap_error}")
endif()
get_target_property(_esp32qjs_eap_sources ${_esp32qjs_eap_lib} SOURCES)
set(_esp32qjs_eap_new_sources "")
set(_esp32qjs_eap_client_count 0)
set(_esp32qjs_eap_peer_count 0)
set(_esp32qjs_eap_wpa_count 0)
foreach(_esp32qjs_eap_entry IN LISTS _esp32qjs_eap_sources)
    get_filename_component(_esp32qjs_eap_absolute "${_esp32qjs_eap_entry}" ABSOLUTE BASE_DIR "${_esp32qjs_eap_dir}")
    if(_esp32qjs_eap_absolute STREQUAL "${_esp32qjs_eap_dir}/esp_supplicant/src/esp_eap_client.c")
        list(APPEND _esp32qjs_eap_new_sources "${_esp32qjs_eap_output}/esp_eap_client.c")
        math(EXPR _esp32qjs_eap_client_count "${_esp32qjs_eap_client_count} + 1")
    elseif(_esp32qjs_eap_absolute STREQUAL "${_esp32qjs_eap_dir}/src/eap_peer/eap.c")
        list(APPEND _esp32qjs_eap_new_sources "${_esp32qjs_eap_output}/eap.c")
        math(EXPR _esp32qjs_eap_peer_count "${_esp32qjs_eap_peer_count} + 1")
    elseif(_esp32qjs_eap_absolute STREQUAL "${_esp32qjs_eap_dir}/esp_supplicant/src/esp_wpa_main.c")
        list(APPEND _esp32qjs_eap_new_sources "${_esp32qjs_eap_output}/esp_wpa_main.c")
        math(EXPR _esp32qjs_eap_wpa_count "${_esp32qjs_eap_wpa_count} + 1")
    else()
        list(APPEND _esp32qjs_eap_new_sources "${_esp32qjs_eap_entry}")
    endif()
endforeach()
if(NOT _esp32qjs_eap_client_count EQUAL 1 OR NOT _esp32qjs_eap_peer_count EQUAL 1 OR NOT _esp32qjs_eap_wpa_count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one EAP client, peer and WPA main source in SDK component")
endif()
set_property(TARGET ${_esp32qjs_eap_lib} PROPERTY SOURCES "${_esp32qjs_eap_new_sources}")
string(STRIP "${_esp32qjs_eap_message}" _esp32qjs_eap_message)
message(STATUS "${_esp32qjs_eap_message}")
