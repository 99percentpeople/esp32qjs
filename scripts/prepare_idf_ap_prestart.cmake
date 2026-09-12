if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI OR NOT CONFIG_ESP_WIFI_SOFTAP_SUPPORT OR CONFIG_ESP_HOST_WIFI_ENABLED OR
   NOT IDF_TARGET MATCHES "^esp32(c3|c5|s3)$")
    return()
endif()
# The private clone is built from the original archive. Other SDK fixes may
# replace imported build archives later; the shared IDF installation stays intact.
idf_component_get_property(_ap_prestart_sdk esp_wifi COMPONENT_DIR)
idf_component_get_property(_ap_prestart_framework esp32_mquickjs COMPONENT_LIB)
idf_build_get_property(_ap_prestart_python PYTHON)
set(_ap_prestart_source "${_ap_prestart_sdk}/lib/${IDF_TARGET}/libnet80211.a")
set(_ap_prestart_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/ap_prestart.o")
set(_ap_prestart_script "${CMAKE_CURRENT_LIST_DIR}/prepare_idf_ap_prestart.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_ap_prestart_source}" "${_ap_prestart_script}")
execute_process(COMMAND "${_ap_prestart_python}" "${_ap_prestart_script}"
    --source "${_ap_prestart_source}" --output "${_ap_prestart_output}" --target "${IDF_TARGET}"
    --ar "${CMAKE_AR}" --objcopy "${CMAKE_OBJCOPY}" --linker "${CMAKE_LINKER}"
    RESULT_VARIABLE _ap_prestart_result OUTPUT_VARIABLE _ap_prestart_message ERROR_VARIABLE _ap_prestart_error)
if(NOT _ap_prestart_result EQUAL 0)
    message(FATAL_ERROR "AP pre-start preparation failed: ${_ap_prestart_error}")
endif()
set_source_files_properties("${_ap_prestart_output}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
target_sources(${_ap_prestart_framework} PRIVATE "${_ap_prestart_output}")
target_link_libraries(${_ap_prestart_framework} INTERFACE "-Wl,--wrap=wifi_mode_set")
string(STRIP "${_ap_prestart_message}" _ap_prestart_message)
message(STATUS "${_ap_prestart_message}")
