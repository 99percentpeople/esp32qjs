# Apply after other net80211 consumers: only the reviewed API member changes.
if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI OR CONFIG_ESP_HOST_WIFI_ENABLED)
    return()
endif()
if(NOT TARGET esp_wifi_net80211 OR NOT IDF_TARGET MATCHES "^esp32(c3|c5|s3)$")
    message(FATAL_ERROR "TX rate configuration requires a reviewed local SDK")
endif()
get_target_property(_qjs_rate_source esp_wifi_net80211 IMPORTED_LOCATION)
idf_build_get_property(_qjs_rate_python PYTHON)
set(_qjs_rate_script "${CMAKE_CURRENT_LIST_DIR}/patch_idf_tx_rate.py")
# Preserve the basename used by SDK linker-fragment IRAM placement rules.
set(_qjs_rate_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/tx-rate/libnet80211.a")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_qjs_rate_source}" "${_qjs_rate_script}"
    "${CMAKE_CURRENT_LIST_DIR}/patch_idf_twt_probe_wake.py")
execute_process(COMMAND "${_qjs_rate_python}" "${_qjs_rate_script}"
    --source "${_qjs_rate_source}" --output "${_qjs_rate_output}" --target "${IDF_TARGET}"
    RESULT_VARIABLE _qjs_rate_result OUTPUT_VARIABLE _qjs_rate_message ERROR_VARIABLE _qjs_rate_error)
if(NOT _qjs_rate_result EQUAL 0)
    message(FATAL_ERROR "TX rate SDK repair failed: ${_qjs_rate_error}")
endif()
set_property(TARGET esp_wifi_net80211 PROPERTY IMPORTED_LOCATION "${_qjs_rate_output}")
idf_component_get_property(_qjs_rate_framework esp32_mquickjs COMPONENT_LIB)
# IDF places proprietary Wi-Fi archives after the framework archive group.
# Retain this SDK-only callback before that group has finished being scanned.
target_link_libraries(${_qjs_rate_framework} INTERFACE "-Wl,--undefined=esp32qjs_wifi_tx_rate_context")
string(STRIP "${_qjs_rate_message}" _qjs_rate_message)
message(STATUS "${_qjs_rate_message}")
