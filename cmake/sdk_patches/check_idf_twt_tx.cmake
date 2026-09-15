# TWT buffer identity/layout and the native recycler require BOTH archives.
# This is a read-only gate; shared SDK archives are never rewritten here.
if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI OR NOT CONFIG_SOC_WIFI_HE_SUPPORT OR NOT CONFIG_IDF_TARGET_ESP32C5)
    return()
endif()
if(NOT TARGET esp_wifi_pp OR CONFIG_ESP_HOST_WIFI_ENABLED)
    message(FATAL_ERROR "TWT TX observation requires the reviewed local C5 PP archive")
endif()
get_target_property(_esp32qjs_twt_pp esp_wifi_pp IMPORTED_LOCATION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_esp32qjs_twt_pp}")
file(SHA256 "${_esp32qjs_twt_pp}" _esp32qjs_twt_pp_hash)
if(NOT _esp32qjs_twt_pp_hash STREQUAL "${ESP32QJS_SDK_SHA_ESP_WIFI_LIB_ESP32C5_LIBPP_A}")
    message(FATAL_ERROR "Unreviewed C5 libpp.a SHA-256 ${_esp32qjs_twt_pp_hash}; review TWT TX callback/recycle paths")
endif()
# C5 resolves the recycler/PP completion to ROM trampolines, whose reviewed
# rev0/rev100 implementations dispatch through pp_wdev_funcs + 304. The PP
# archive's wdev_funcs_init stores the wrapped recycler in that table entry.
# Reviewing only discarded archive bodies would miss this actual call chain.
idf_build_get_property(_esp32qjs_twt_idf IDF_PATH)
foreach(_esp32qjs_twt_rom IN ITEMS pp net80211)
    set(_esp32qjs_twt_rom_file "${_esp32qjs_twt_idf}/components/esp_rom/esp32c5/ld/esp32c5.rom.${_esp32qjs_twt_rom}.ld")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_esp32qjs_twt_rom_file}")
    file(SHA256 "${_esp32qjs_twt_rom_file}" _esp32qjs_twt_rom_hash)
    if(_esp32qjs_twt_rom STREQUAL "pp")
        set(_esp32qjs_twt_expected "${ESP32QJS_SDK_SHA_ESP_ROM_ESP32C5_LD_ESP32C5_ROM_PP_LD}")
    else()
        set(_esp32qjs_twt_expected "${ESP32QJS_SDK_SHA_ESP_ROM_ESP32C5_LD_ESP32C5_ROM_NET80211_LD}")
    endif()
    if(NOT _esp32qjs_twt_rom_hash STREQUAL _esp32qjs_twt_expected)
        message(FATAL_ERROR "Unreviewed C5 ${_esp32qjs_twt_rom} ROM linker SHA-256 ${_esp32qjs_twt_rom_hash}; review TWT TX dispatch")
    endif()
endforeach()
set(_esp32qjs_twt_adapter_files
    "components/esp_wifi/esp32c5/esp_adapter.c"
    "components/esp_coex/esp32c5/esp_coex_adapter.c"
    "components/esp_timer/src/ets_timer_legacy.c"
    "components/esp_timer/src/esp_timer.c")
set(_esp32qjs_twt_adapter_hashes
    "${ESP32QJS_SDK_SHA_ESP_WIFI_ESP32C5_ESP_ADAPTER_C}"
    "${ESP32QJS_SDK_SHA_ESP_COEX_ESP32C5_ESP_COEX_ADAPTER_C}"
    "${ESP32QJS_SDK_SHA_ESP_TIMER_SRC_ETS_TIMER_LEGACY_C}"
    "${ESP32QJS_SDK_SHA_ESP_TIMER_SRC_ESP_TIMER_C}")
foreach(_esp32qjs_twt_index RANGE 0 3)
    list(GET _esp32qjs_twt_adapter_files ${_esp32qjs_twt_index} _esp32qjs_twt_relative)
    list(GET _esp32qjs_twt_adapter_hashes ${_esp32qjs_twt_index} _esp32qjs_twt_expected)
    set(_esp32qjs_twt_file "${_esp32qjs_twt_idf}/${_esp32qjs_twt_relative}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_esp32qjs_twt_file}")
    file(SHA256 "${_esp32qjs_twt_file}" _esp32qjs_twt_hash)
    if(NOT _esp32qjs_twt_hash STREQUAL _esp32qjs_twt_expected)
        message(FATAL_ERROR "Unreviewed TWT timer adapter ${_esp32qjs_twt_relative} SHA-256 ${_esp32qjs_twt_hash}")
    endif()
endforeach()
message(STATUS "ESP32QJS TWT TX: reviewed C5 PP archive and ROM linkers; net80211 gated by Vendor IE fix")
