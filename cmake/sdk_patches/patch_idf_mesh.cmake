# Mesh events update native control state before the SDK's nonblocking post.
# SDK libraries and source remain unchanged; only the final link is wrapped.
if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI OR NOT CONFIG_SOC_WIFI_MESH_SUPPORT OR
   NOT CONFIG_ESP_WIFI_SOFTAP_SUPPORT OR NOT CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API OR
   NOT CONFIG_LWIP_IPV4 OR CONFIG_ESP_HOST_WIFI_ENABLED)
    return()
endif()
set(_mesh_expected "")
if(IDF_TARGET STREQUAL "esp32c3")
    set(_mesh_expected "${ESP32QJS_SDK_SHA_ESP_WIFI_LIB_ESP32C3_LIBMESH_A}")
elseif(IDF_TARGET STREQUAL "esp32c5")
    set(_mesh_expected "${ESP32QJS_SDK_SHA_ESP_WIFI_LIB_ESP32C5_LIBMESH_A}")
elseif(IDF_TARGET STREQUAL "esp32s3")
    set(_mesh_expected "${ESP32QJS_SDK_SHA_ESP_WIFI_LIB_ESP32S3_LIBMESH_A}")
else()
    return()
endif()
idf_component_get_property(_mesh_dir esp_wifi COMPONENT_DIR)
set(_mesh_library "${_mesh_dir}/lib/${IDF_TARGET}/libmesh.a")
file(SHA256 "${_mesh_library}" _mesh_hash)
file(SHA256 "${_mesh_dir}/src/mesh_event.c" _mesh_event_hash)
file(SHA256 "${_mesh_dir}/include/esp_mesh.h" _mesh_header_hash)
file(SHA256 "${_mesh_dir}/include/esp_mesh_internal.h" _mesh_internal_hash)
if(NOT _mesh_hash STREQUAL _mesh_expected OR
   NOT _mesh_internal_hash STREQUAL "${ESP32QJS_SDK_SHA_ESP_WIFI_INCLUDE_ESP_MESH_INTERNAL_H}" OR
   NOT _mesh_event_hash STREQUAL "${ESP32QJS_SDK_SHA_ESP_WIFI_SRC_MESH_EVENT_C}" OR
   NOT _mesh_header_hash STREQUAL "${ESP32QJS_SDK_SHA_ESP_WIFI_INCLUDE_ESP_MESH_H}")
    message(FATAL_ERROR "Unreviewed Mesh library, event boundary or public ABI")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_mesh_library}" "${_mesh_dir}/src/mesh_event.c" "${_mesh_dir}/include/esp_mesh.h" "${_mesh_dir}/include/esp_mesh_internal.h")
idf_component_get_property(_mesh_framework_lib esp32_mquickjs COMPONENT_LIB)
target_link_libraries(${_mesh_framework_lib} INTERFACE "-Wl,--wrap=esp_mesh_send_event_internal")
