if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI OR NOT IDF_TARGET STREQUAL "esp32c5" OR CONFIG_ESP_HOST_WIFI_ENABLED)
    return()
endif()
idf_component_get_property(_he_stats_wifi esp_wifi COMPONENT_DIR)
idf_component_get_property(_he_stats_rom esp_rom COMPONENT_DIR)
idf_component_get_property(_he_stats_framework esp32_mquickjs COMPONENT_LIB)
set(_he_stats_archive "${_he_stats_wifi}/lib/esp32c5/libpp.a")
set(_he_stats_rom_layout "${_he_stats_rom}/esp32c5/ld/esp32c5.rom.pp.ld")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_he_stats_archive}" "${_he_stats_rom_layout}")
file(SHA256 "${_he_stats_archive}" _he_stats_code_hash)
file(SHA256 "${_he_stats_rom_layout}" _he_stats_rom_hash)
if(NOT _he_stats_code_hash STREQUAL "a06374436b70eb6b8a2b8d653976218db6e08e7dbe90c8414b07ad9e923d9f99" OR
   NOT _he_stats_rom_hash STREQUAL "93b0ac5f5daf8b1b3daeb87b01e1a5a56fea20de3d58a16ff3685c15ce1242c7")
    message(FATAL_ERROR "Review HE statistics HAL allocation/error/ROM layout adapter for this ESP-IDF")
endif()
target_link_libraries(${_he_stats_framework} INTERFACE
    "-Wl,--wrap=hal_enable_rx_statistics" "-Wl,--wrap=hal_enable_tx_statistics"
    "-Wl,--undefined=__wrap_hal_enable_rx_statistics" "-Wl,--undefined=__wrap_hal_enable_tx_statistics")
