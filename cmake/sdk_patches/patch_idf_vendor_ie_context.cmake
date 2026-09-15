# The SDK's reviewed Vendor IE ioctl handler stores &message.ctx instead of ctx.
# Keep SDK and immutable Context untouched; replace only the imported build input.
if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI)
    return()
endif()
if(NOT TARGET esp_wifi_net80211 OR CONFIG_ESP_HOST_WIFI_ENABLED)
    message(FATAL_ERROR "Vendor IE callback context requires a reviewed local Wi-Fi SDK archive")
endif()
get_target_property(_esp32qjs_vendor_archive esp_wifi_net80211 IMPORTED_LOCATION)
idf_build_get_property(_esp32qjs_vendor_python PYTHON)
set(_esp32qjs_vendor_script "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wifi/vendor_ie_context.py")
set(_esp32qjs_vendor_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/libnet80211.a")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_esp32qjs_vendor_archive}" "${_esp32qjs_vendor_script}"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wifi/twt_probe_wake.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wifi/offchan_frame.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wifi/nan_sd.py")
set(_esp32qjs_vendor_extra_args)
if(CONFIG_ESP_WIFI_NAN_SYNC_ENABLE)
    list(APPEND _esp32qjs_vendor_extra_args --nan-sd-buffer-fix)
endif()
if(CONFIG_SOC_WIFI_HE_SUPPORT AND CONFIG_IDF_TARGET_ESP32C5)
    list(APPEND _esp32qjs_vendor_extra_args --twt-probe-buffer-fix --twt-probe-wake-fix)
endif()
if(CONFIG_ESP_WIFI_FTM_ENABLE AND CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT)
    list(APPEND _esp32qjs_vendor_extra_args --ftm-report-null-fix)
endif()
if(CONFIG_ESP_WIFI_DPP_SUPPORT OR CONFIG_ESP_WIFI_NAN_USD_ENABLE)
    include("${CMAKE_CURRENT_LIST_DIR}/check_idf_offchan_frame.cmake")
    list(APPEND _esp32qjs_vendor_extra_args --offchan-frame-fix)
endif()
execute_process(
    COMMAND "${_esp32qjs_vendor_python}" "${CMAKE_SOURCE_DIR}/scripts/sdk_patch.py" vendor_ie_context
        --source "${_esp32qjs_vendor_archive}" --output "${_esp32qjs_vendor_output}" --target "${IDF_TARGET}" ${_esp32qjs_vendor_extra_args}
    RESULT_VARIABLE _esp32qjs_vendor_result
    OUTPUT_VARIABLE _esp32qjs_vendor_message
    ERROR_VARIABLE _esp32qjs_vendor_error)
if(NOT _esp32qjs_vendor_result EQUAL 0)
    message(FATAL_ERROR "ESP32QJS Vendor IE context fix failed: ${_esp32qjs_vendor_error}")
endif()
set_property(TARGET esp_wifi_net80211 PROPERTY IMPORTED_LOCATION "${_esp32qjs_vendor_output}")
string(STRIP "${_esp32qjs_vendor_message}" _esp32qjs_vendor_message)
message(STATUS "${_esp32qjs_vendor_message}")
