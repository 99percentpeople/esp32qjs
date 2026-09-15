# Keep SmartConfig decoder/ACK fixes in the immutable build's generated sources.
# The SDK ACK implementation is only compiled with the BSD/IPv4 backend.
if(NOT TARGET idf::esp_wifi OR NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI OR
   NOT CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API OR NOT CONFIG_LWIP_IPV4 OR
   NOT CONFIG_ESP_WIFI_ENABLED)
    return()
endif()
idf_component_get_property(_sc_lib esp_wifi COMPONENT_LIB)
idf_component_get_property(_sc_dir esp_wifi COMPONENT_DIR)
idf_build_get_property(_sc_python PYTHON)
idf_build_get_property(_sc_target IDF_TARGET)
set(_sc_script "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wifi/smartconfig.py")
set(_sc_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/smartconfig")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_sc_script}"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wifi/smartconfig_stack.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wifi/twt_probe_wake.py")
foreach(_sc_input IN ITEMS src/smartconfig.c src/smartconfig_ack.c
        include/esp_smartconfig.h include/smartconfig_ack.h CMakeLists.txt)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_sc_dir}/${_sc_input}")
endforeach()
foreach(_sc_review_target IN ITEMS esp32c3 esp32s3 esp32c5)
    foreach(_sc_input IN ITEMS "lib/${_sc_review_target}/libsmartconfig.a" "lib/${_sc_review_target}/libnet80211.a"
            "${_sc_review_target}/esp_adapter.c" "../esp_coex/${_sc_review_target}/esp_coex_adapter.c")
        get_filename_component(_sc_dependency "${_sc_dir}/${_sc_input}" REALPATH)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_sc_dependency}")
    endforeach()
endforeach()
foreach(_sc_input IN ITEMS "../esp_timer/src/ets_timer_legacy.c"
        "../wpa_supplicant/esp_supplicant/src/esp_wifi_driver.h"
        "include/esp_private/wifi_os_adapter.h" "../heap/include/esp_heap_caps.h"
        "../heap/heap_caps.c")
    # Ninja canonicalizes '..' in paths. Register the same canonical spelling
    # as other SDK checks, otherwise regeneration emits duplicate phony outputs.
    get_filename_component(_sc_dependency "${_sc_dir}/${_sc_input}" REALPATH)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_sc_dependency}")
endforeach()
if(NOT TARGET esp_wifi_smartconfig OR CONFIG_ESP_HOST_WIFI_ENABLED)
    message(FATAL_ERROR "SmartConfig cleanup requires the reviewed local SDK archive")
endif()
execute_process(COMMAND "${_sc_python}" "${CMAKE_SOURCE_DIR}/scripts/sdk_patch.py" smartconfig
    --component "${_sc_dir}" --output-dir "${_sc_output}"
    --target "${_sc_target}" --objcopy "${CMAKE_OBJCOPY}"
    RESULT_VARIABLE _sc_result OUTPUT_VARIABLE _sc_message ERROR_VARIABLE _sc_error)
if(NOT _sc_result EQUAL 0)
    message(FATAL_ERROR "ESP32QJS SmartConfig fix failed: ${_sc_error}")
endif()
get_target_property(_sc_sources ${_sc_lib} SOURCES)
set(_sc_new_sources "")
set(_sc_count 0)
set(_sc_adapter_count 0)
foreach(_sc_source IN LISTS _sc_sources)
    get_filename_component(_sc_absolute "${_sc_source}" ABSOLUTE BASE_DIR "${_sc_dir}")
    if(_sc_absolute STREQUAL "${_sc_dir}/src/smartconfig.c" OR
       _sc_absolute STREQUAL "${_sc_dir}/src/smartconfig_ack.c")
        get_filename_component(_sc_name "${_sc_absolute}" NAME)
        list(APPEND _sc_new_sources "${_sc_output}/${_sc_name}")
        math(EXPR _sc_count "${_sc_count} + 1")
    elseif(_sc_absolute STREQUAL "${_sc_dir}/${_sc_target}/esp_adapter.c")
        list(APPEND _sc_new_sources "${_sc_output}/esp_adapter.c")
        math(EXPR _sc_adapter_count "${_sc_adapter_count} + 1")
    else()
        list(APPEND _sc_new_sources "${_sc_source}")
    endif()
endforeach()
if(NOT _sc_count EQUAL 2 OR NOT _sc_adapter_count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one SmartConfig wrapper, ACK source and Wi-Fi adapter")
endif()
set_property(TARGET ${_sc_lib} PROPERTY SOURCES "${_sc_new_sources}")
set_property(TARGET esp_wifi_smartconfig PROPERTY IMPORTED_LOCATION "${_sc_output}/libsmartconfig.a")
string(STRIP "${_sc_message}" _sc_message)
message(STATUS "${_sc_message}")
