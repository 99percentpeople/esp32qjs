# Run after prior SDK fixes; preserve their patched members and shared SDK.
if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI)
    return()
endif()
if(CONFIG_ESP_HOST_WIFI_ENABLED OR NOT TARGET esp_wifi_net80211 OR NOT IDF_TARGET MATCHES "^esp32(c3|c5|s3)$")
    message(FATAL_ERROR "Raw TX management extension requires a reviewed local SDK")
endif()
get_target_property(_qjs_mgmt_source esp_wifi_net80211 IMPORTED_LOCATION)
idf_build_get_property(_qjs_mgmt_python PYTHON)
set(_qjs_mgmt_script "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wifi/raw_tx_management.py")
set(_qjs_mgmt_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/raw-tx-management/libnet80211.a")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_qjs_mgmt_source}" "${_qjs_mgmt_script}"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wifi/twt_probe_wake.py")
execute_process(COMMAND "${_qjs_mgmt_python}" "${CMAKE_SOURCE_DIR}/scripts/sdk_patch.py" raw_tx_management
    --source "${_qjs_mgmt_source}" --output "${_qjs_mgmt_output}" --target "${IDF_TARGET}"
    RESULT_VARIABLE _qjs_mgmt_result OUTPUT_VARIABLE _qjs_mgmt_message ERROR_VARIABLE _qjs_mgmt_error)
if(NOT _qjs_mgmt_result EQUAL 0)
    message(FATAL_ERROR "Raw TX management SDK extension failed: ${_qjs_mgmt_error}")
endif()
set_property(TARGET esp_wifi_net80211 PROPERTY IMPORTED_LOCATION "${_qjs_mgmt_output}")
idf_component_get_property(_qjs_mgmt_framework esp32_mquickjs COMPONENT_LIB)
# Advertise/admit the extension only after this exact build's SDK gate succeeds.
target_compile_definitions(${_qjs_mgmt_framework} PRIVATE ESP32_MQUICKJS_RAW_TX_EXTENDED_MANAGEMENT=1)
string(STRIP "${_qjs_mgmt_message}" _qjs_mgmt_message)
message(STATUS "${_qjs_mgmt_message}")
