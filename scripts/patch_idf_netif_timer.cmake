# Apply after project(): replace one esp_netif source with a generated build-local
# copy. Do not mutate a shared IDF checkout or weaken the immutable Build Context.
if(NOT TARGET idf::esp_netif OR NOT CONFIG_ESP_NETIF_TCPIP_LWIP OR
   NOT CONFIG_LWIP_IPV4 OR NOT CONFIG_ESP_NETIF_LOST_IP_TIMER_ENABLE)
    return()
endif()

idf_component_get_property(_esp32qjs_netif_lib esp_netif COMPONENT_LIB)
idf_component_get_property(_esp32qjs_netif_dir esp_netif COMPONENT_DIR)
idf_build_get_property(_esp32qjs_netif_python PYTHON)
set(_esp32qjs_netif_source "${_esp32qjs_netif_dir}/lwip/esp_netif_lwip.c")
set(_esp32qjs_netif_patch_script "${CMAKE_CURRENT_LIST_DIR}/patch_idf_netif_timer.py")
set(_esp32qjs_netif_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/esp_netif_lwip.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_esp32qjs_netif_source}" "${_esp32qjs_netif_patch_script}")
execute_process(
    COMMAND "${_esp32qjs_netif_python}" "${_esp32qjs_netif_patch_script}"
        --source "${_esp32qjs_netif_source}" --output "${_esp32qjs_netif_output}"
    RESULT_VARIABLE _esp32qjs_netif_patch_result
    OUTPUT_VARIABLE _esp32qjs_netif_patch_message
    ERROR_VARIABLE _esp32qjs_netif_patch_error)
if(NOT _esp32qjs_netif_patch_result EQUAL 0)
    message(FATAL_ERROR "ESP32QJS netif timer fix failed: ${_esp32qjs_netif_patch_error}")
endif()

get_target_property(_esp32qjs_netif_sources ${_esp32qjs_netif_lib} SOURCES)
set(_esp32qjs_netif_replacements 0)
set(_esp32qjs_netif_new_sources "")
foreach(_esp32qjs_netif_entry IN LISTS _esp32qjs_netif_sources)
    get_filename_component(_esp32qjs_netif_absolute "${_esp32qjs_netif_entry}"
        ABSOLUTE BASE_DIR "${_esp32qjs_netif_dir}")
    if(_esp32qjs_netif_absolute STREQUAL _esp32qjs_netif_source)
        list(APPEND _esp32qjs_netif_new_sources "${_esp32qjs_netif_output}")
        math(EXPR _esp32qjs_netif_replacements "${_esp32qjs_netif_replacements} + 1")
    else()
        list(APPEND _esp32qjs_netif_new_sources "${_esp32qjs_netif_entry}")
    endif()
endforeach()
if(NOT _esp32qjs_netif_replacements EQUAL 1)
    message(FATAL_ERROR "Expected exactly one esp_netif_lwip.c in the SDK component")
endif()
set_property(TARGET ${_esp32qjs_netif_lib} PROPERTY SOURCES "${_esp32qjs_netif_new_sources}")
string(STRIP "${_esp32qjs_netif_patch_message}" _esp32qjs_netif_patch_message)
message(STATUS "${_esp32qjs_netif_patch_message}")
