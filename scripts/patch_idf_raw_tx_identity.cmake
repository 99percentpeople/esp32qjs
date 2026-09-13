# Run after prior SDK fixes; preserve their patched members and shared SDK.
if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI)
    return()
endif()
if(CONFIG_ESP_HOST_WIFI_ENABLED OR NOT TARGET esp_wifi_net80211 OR NOT IDF_TARGET MATCHES "^esp32(c3|c5|s3)$")
    message(FATAL_ERROR "Raw TX descriptor identity hooks requires a reviewed local SDK")
endif()
get_target_property(_qjs_identity_source esp_wifi_net80211 IMPORTED_LOCATION)
get_target_property(_qjs_identity_pp esp_wifi_pp IMPORTED_LOCATION)
idf_build_get_property(_qjs_identity_python PYTHON)
set(_qjs_identity_script "${CMAKE_CURRENT_LIST_DIR}/patch_idf_raw_tx_identity.py")
set(_qjs_identity_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/raw-tx-identity/libnet80211.a")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_qjs_identity_source}" "${_qjs_identity_pp}" "${_qjs_identity_script}"
    "${CMAKE_CURRENT_LIST_DIR}/patch_idf_twt_probe_wake.py")
execute_process(COMMAND "${_qjs_identity_python}" "${_qjs_identity_script}"
    --source "${_qjs_identity_source}" --pp-source "${_qjs_identity_pp}" --output "${_qjs_identity_output}" --target "${IDF_TARGET}"
    RESULT_VARIABLE _qjs_identity_result OUTPUT_VARIABLE _qjs_identity_message ERROR_VARIABLE _qjs_identity_error)
if(NOT _qjs_identity_result EQUAL 0)
    message(FATAL_ERROR "Raw TX descriptor identity hooks failed: ${_qjs_identity_error}")
endif()
set_property(TARGET esp_wifi_net80211 PROPERTY IMPORTED_LOCATION "${_qjs_identity_output}")
idf_component_get_property(_qjs_identity_framework esp32_mquickjs COMPONENT_LIB)
# Enable multiple in-flight identities only for this reviewed SDK adapter.
target_compile_definitions(${_qjs_identity_framework} PRIVATE ESP32_MQUICKJS_RAW_TX_DESCRIPTOR_IDENTITY=1)
string(STRIP "${_qjs_identity_message}" _qjs_identity_message)
message(STATUS "${_qjs_identity_message}")
