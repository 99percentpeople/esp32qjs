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
    set(_esp32qjs_frame_pp_expected "756aa3ba9fcd8bef1ee5a8ec36c82406b5ed3a4d43f94ef6b15e05b2cd7a593c")
elseif(IDF_TARGET STREQUAL "esp32s3")
    set(_esp32qjs_frame_pp_expected "ae7004d00c2c5e1cf106548b22e2ed5bb3a059d65393f4e15791019658191cdb")
elseif(IDF_TARGET STREQUAL "esp32c5")
    set(_esp32qjs_frame_pp_expected "a06374436b70eb6b8a2b8d653976218db6e08e7dbe90c8414b07ad9e923d9f99")
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
        "62dbb8b247b8f1f55a2637d836e045d301b8c8553a43f5b1e5fcc67acc3001ee"
        "da03bcb48016c2c7c4cc776adcf66fb87356787e4c8d4e0ad64d72a2a454696a"
        "02a1101d9f34315679b43da70e1cbb8cc175594c21f47a1576e62603678f2063")
elseif(IDF_TARGET STREQUAL "esp32s3")
    set(_esp32qjs_frame_rom_files "esp32s3.rom.ld")
    set(_esp32qjs_frame_rom_hashes "3abed7ef1f85a810335b8f639ddee19f18fa9f7083253601548c25a17af62dc9")
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
    set(_esp32qjs_chm_wifi_hash "06973e30a235812f837fe32fd91ee41b32bf889b3813f491746666870e8af45f")
    set(_esp32qjs_chm_coex_hash "48bccbf6b585ad7e6634d1bba26f9a83eca32371352aeeb9fbe9bef97d28bd12")
elseif(IDF_TARGET STREQUAL "esp32s3")
    set(_esp32qjs_chm_wifi_hash "aedf22d0832963eaf206ef2571c46ed515a91f72f7d017c46ec830ce8efdd3a0")
    set(_esp32qjs_chm_coex_hash "7fb34f00fb22a158b73252f4be55800b429623153a050998b5e3a1fb015b5c1d")
else()
    set(_esp32qjs_chm_wifi_hash "cd43bb333b2fb7cc38590560b1f1fadf5a1aae1314d740185dc7626d8d1b30cc")
    set(_esp32qjs_chm_coex_hash "8a2a75be1e1721d29ee58b6f40c44747928c37fc81b25d7a434b2d7f03c0fa21")
endif()
set(_esp32qjs_chm_paths
    "components/esp_wifi/${IDF_TARGET}/esp_adapter.c"
    "components/esp_coex/${IDF_TARGET}/esp_coex_adapter.c"
    "components/esp_timer/src/esp_timer.c" "components/esp_timer/src/ets_timer_legacy.c")
set(_esp32qjs_chm_hashes "${_esp32qjs_chm_wifi_hash}" "${_esp32qjs_chm_coex_hash}"
    "5063132e2fa243d69b85b9bc1c93bb19d79110b265da07b5e6a4b779f66c563e"
    "fb28b0f5b2d15950bde406cd327992678c52300cef39e5f9462259f2fd2d083f")
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
