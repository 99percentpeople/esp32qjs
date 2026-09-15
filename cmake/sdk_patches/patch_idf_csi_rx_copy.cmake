# Run after other consumers validate the original libpp.a. Modify only the
# imported build input, never the SDK or immutable Build Context.
if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI)
    return()
endif()
if(NOT TARGET esp_wifi_pp OR CONFIG_ESP_HOST_WIFI_ENABLED)
    message(FATAL_ERROR "CSI RX copy receipts require a reviewed local Wi-Fi archive")
endif()
get_target_property(_qjs_csi_pp esp_wifi_pp IMPORTED_LOCATION)
idf_build_get_property(_qjs_csi_python PYTHON)
set(_qjs_csi_script "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wifi/csi_rx_copy.py")
set(_qjs_csi_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/libpp.a")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_qjs_csi_pp}" "${_qjs_csi_script}"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wifi/twt_probe_wake.py")
execute_process(COMMAND "${_qjs_csi_python}" "${CMAKE_SOURCE_DIR}/scripts/sdk_patch.py" csi_rx_copy
    --source "${_qjs_csi_pp}" --output "${_qjs_csi_output}" --target "${IDF_TARGET}"
    RESULT_VARIABLE _qjs_csi_result OUTPUT_VARIABLE _qjs_csi_message ERROR_VARIABLE _qjs_csi_error)
if(NOT _qjs_csi_result EQUAL 0)
    message(FATAL_ERROR "CSI RX copy receipt patch failed: ${_qjs_csi_error}")
endif()
set_property(TARGET esp_wifi_pp PROPERTY IMPORTED_LOCATION "${_qjs_csi_output}")
string(STRIP "${_qjs_csi_message}" _qjs_csi_message)
message(STATUS "${_qjs_csi_message}")
