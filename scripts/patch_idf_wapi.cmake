# WAPI remains owned by the SDK supplicant. Wrap its existing lifecycle calls;
# do not initialize another callback table from a JS operation.
if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI OR NOT CONFIG_ESP_WIFI_WAPI_PSK)
    return()
endif()
if(NOT TARGET idf::esp_wifi OR NOT TARGET idf::wpa_supplicant OR CONFIG_ESP_HOST_WIFI_ENABLED)
    message(FATAL_ERROR "WAPI control requires the reviewed local supplicant")
endif()
idf_component_get_property(_wapi_wifi_dir esp_wifi COMPONENT_DIR)
idf_component_get_property(_wapi_wpa_dir wpa_supplicant COMPONENT_DIR)
set(_wapi_library "${_wapi_wifi_dir}/lib/${IDF_TARGET}/libwapi.a")
set(_wapi_expected "")
if(IDF_TARGET STREQUAL "esp32c3" OR IDF_TARGET STREQUAL "esp32c5")
    set(_wapi_expected "72f37d369a015ff7db679710f7d859cea89189800924df9103ab83c9f565e663")
elseif(IDF_TARGET STREQUAL "esp32s3")
    set(_wapi_expected "47c29d31b915377e2e1298d2baf2f841e8f72f2e8356d9c41f2bf4257367f2f1")
endif()
file(SHA256 "${_wapi_library}" _wapi_hash)
file(SHA256 "${_wapi_wpa_dir}/esp_supplicant/src/esp_wpa_main.c" _wapi_main_hash)
if(NOT _wapi_hash STREQUAL _wapi_expected OR NOT _wapi_main_hash STREQUAL "c4c6ff42611816ef389501174bf84906585f1e81e9ef2369bca0b79d02487d17")
    message(FATAL_ERROR "Unreviewed WAPI library or supplicant lifecycle")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_wapi_library}" "${_wapi_wpa_dir}/esp_supplicant/src/esp_wpa_main.c")
idf_component_get_property(_wapi_framework_lib esp32_mquickjs COMPONENT_LIB)
target_link_libraries(${_wapi_framework_lib} INTERFACE
    "-Wl,--wrap=esp_wifi_internal_wapi_init"
    "-Wl,--wrap=esp_wifi_internal_wapi_deinit")
