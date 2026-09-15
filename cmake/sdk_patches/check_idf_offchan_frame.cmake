# The EB's final release must reach the observer before its address is reused.
# C5 shares the existing TWT recycler wrapper and ROM gate; C3/S3 install the
# observer through the same wdev function table and external PP calls.
if(NOT TARGET esp_wifi_pp OR CONFIG_ESP_HOST_WIFI_ENABLED)
    message(FATAL_ERROR "Off-channel frame identity requires a reviewed local PP archive")
endif()
get_target_property(_esp32qjs_frame_pp esp_wifi_pp IMPORTED_LOCATION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_esp32qjs_frame_pp}")
file(SHA256 "${_esp32qjs_frame_pp}" _esp32qjs_frame_pp_hash)
if(IDF_TARGET STREQUAL "esp32c3")
    set(_esp32qjs_frame_pp_expected "${ESP32QJS_SDK_SHA_ESP_WIFI_LIB_ESP32C3_LIBPP_A}")
elseif(IDF_TARGET STREQUAL "esp32s3")
    set(_esp32qjs_frame_pp_expected "${ESP32QJS_SDK_SHA_ESP_WIFI_LIB_ESP32S3_LIBPP_A}")
elseif(IDF_TARGET STREQUAL "esp32c5")
    set(_esp32qjs_frame_pp_expected "${ESP32QJS_SDK_SHA_ESP_WIFI_LIB_ESP32C5_LIBPP_A}")
else()
    message(FATAL_ERROR "Unreviewed off-channel frame target ${IDF_TARGET}")
endif()
if(NOT _esp32qjs_frame_pp_hash STREQUAL _esp32qjs_frame_pp_expected)
    message(FATAL_ERROR "Unreviewed ${IDF_TARGET} PP archive for off-channel frame recycle")
endif()
idf_build_get_property(_esp32qjs_frame_idf IDF_PATH)
set(_esp32qjs_frame_rom_files)
set(_esp32qjs_frame_rom_hashes)
if(IDF_TARGET STREQUAL "esp32c3")
    set(_esp32qjs_frame_rom_files "esp32c3.rom.ld" "esp32c3.rom.eco3.ld" "esp32c3.rom.eco7.ld")
    set(_esp32qjs_frame_rom_hashes
        "${ESP32QJS_SDK_SHA_ESP_ROM_ESP32C3_LD_ESP32C3_ROM_LD}"
        "${ESP32QJS_SDK_SHA_ESP_ROM_ESP32C3_LD_ESP32C3_ROM_ECO3_LD}"
        "${ESP32QJS_SDK_SHA_ESP_ROM_ESP32C3_LD_ESP32C3_ROM_ECO7_LD}")
elseif(IDF_TARGET STREQUAL "esp32s3")
    set(_esp32qjs_frame_rom_files "esp32s3.rom.ld")
    set(_esp32qjs_frame_rom_hashes "${ESP32QJS_SDK_SHA_ESP_ROM_ESP32S3_LD_ESP32S3_ROM_LD}")
endif()
foreach(_esp32qjs_frame_rom IN LISTS _esp32qjs_frame_rom_files)
    list(FIND _esp32qjs_frame_rom_files "${_esp32qjs_frame_rom}" _esp32qjs_frame_index)
    list(GET _esp32qjs_frame_rom_hashes ${_esp32qjs_frame_index} _esp32qjs_frame_expected)
    set(_esp32qjs_frame_rom_path "${_esp32qjs_frame_idf}/components/esp_rom/${IDF_TARGET}/ld/${_esp32qjs_frame_rom}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_esp32qjs_frame_rom_path}")
    file(SHA256 "${_esp32qjs_frame_rom_path}" _esp32qjs_frame_hash)
    if(NOT _esp32qjs_frame_hash STREQUAL _esp32qjs_frame_expected)
        message(FATAL_ERROR "Unreviewed off-channel recycle ROM linker ${_esp32qjs_frame_rom}")
    endif()
endforeach()

# CHM's generic wake relies on the native OSI -> legacy ETS dispatch and the
# reviewed esp_timer deadline/delete behavior. DPP can be enabled without the
# IPv4 SmartConfig path, so these checks must not depend on that module's gate.
if(IDF_TARGET STREQUAL "esp32c3")
    set(_esp32qjs_chm_wifi_hash "${ESP32QJS_SDK_SHA_ESP_WIFI_ESP32C3_ESP_ADAPTER_C}")
    set(_esp32qjs_chm_coex_hash "${ESP32QJS_SDK_SHA_ESP_COEX_ESP32C3_ESP_COEX_ADAPTER_C}")
elseif(IDF_TARGET STREQUAL "esp32s3")
    set(_esp32qjs_chm_wifi_hash "${ESP32QJS_SDK_SHA_ESP_WIFI_ESP32S3_ESP_ADAPTER_C}")
    set(_esp32qjs_chm_coex_hash "${ESP32QJS_SDK_SHA_ESP_COEX_ESP32S3_ESP_COEX_ADAPTER_C}")
else()
    set(_esp32qjs_chm_wifi_hash "${ESP32QJS_SDK_SHA_ESP_WIFI_ESP32C5_ESP_ADAPTER_C}")
    set(_esp32qjs_chm_coex_hash "${ESP32QJS_SDK_SHA_ESP_COEX_ESP32C5_ESP_COEX_ADAPTER_C}")
endif()
set(_esp32qjs_chm_paths
    "components/esp_wifi/${IDF_TARGET}/esp_adapter.c"
    "components/esp_coex/${IDF_TARGET}/esp_coex_adapter.c"
    "components/esp_timer/src/esp_timer.c" "components/esp_timer/src/ets_timer_legacy.c")
set(_esp32qjs_chm_hashes "${_esp32qjs_chm_wifi_hash}" "${_esp32qjs_chm_coex_hash}"
    "${ESP32QJS_SDK_SHA_ESP_TIMER_SRC_ESP_TIMER_C}"
    "${ESP32QJS_SDK_SHA_ESP_TIMER_SRC_ETS_TIMER_LEGACY_C}")
foreach(_esp32qjs_chm_index RANGE 0 3)
    list(GET _esp32qjs_chm_paths ${_esp32qjs_chm_index} _esp32qjs_chm_relative)
    list(GET _esp32qjs_chm_hashes ${_esp32qjs_chm_index} _esp32qjs_chm_expected)
    set(_esp32qjs_chm_path "${_esp32qjs_frame_idf}/${_esp32qjs_chm_relative}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_esp32qjs_chm_path}")
    file(SHA256 "${_esp32qjs_chm_path}" _esp32qjs_chm_hash)
    if(NOT _esp32qjs_chm_hash STREQUAL _esp32qjs_chm_expected)
        message(FATAL_ERROR "Unreviewed CHM timer dispatch ${_esp32qjs_chm_relative}")
    endif()
endforeach()
