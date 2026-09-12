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
if(NOT _esp32qjs_twt_pp_hash STREQUAL "a06374436b70eb6b8a2b8d653976218db6e08e7dbe90c8414b07ad9e923d9f99")
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
        set(_esp32qjs_twt_expected "93b0ac5f5daf8b1b3daeb87b01e1a5a56fea20de3d58a16ff3685c15ce1242c7")
    else()
        set(_esp32qjs_twt_expected "c27731095f2c161dbc316c0da81cc02d4d0a3b4bf9a6d046c9dcac7292a789dc")
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
    "cd43bb333b2fb7cc38590560b1f1fadf5a1aae1314d740185dc7626d8d1b30cc"
    "8a2a75be1e1721d29ee58b6f40c44747928c37fc81b25d7a434b2d7f03c0fa21"
    "fb28b0f5b2d15950bde406cd327992678c52300cef39e5f9462259f2fd2d083f"
    "5063132e2fa243d69b85b9bc1c93bb19d79110b265da07b5e6a4b779f66c563e")
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
