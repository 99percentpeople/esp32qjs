# Mesh events update native control state before the SDK's nonblocking post.
# SDK libraries and source remain unchanged; only the final link is wrapped.
if(NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI OR NOT CONFIG_SOC_WIFI_MESH_SUPPORT OR
   NOT CONFIG_ESP_WIFI_SOFTAP_SUPPORT OR NOT CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API OR
   NOT CONFIG_LWIP_IPV4 OR CONFIG_ESP_HOST_WIFI_ENABLED)
    return()
endif()
set(_mesh_expected "")
if(IDF_TARGET STREQUAL "esp32c3")
    set(_mesh_expected "21c20815dd660e6c0964bf0cf15de48b240b08a702c8f29caf106d0746b6bdaa")
elseif(IDF_TARGET STREQUAL "esp32c5")
    set(_mesh_expected "fd1864cc95a78e4151288d9ed0a8204e4f1c21f186c90ce15404c5d064c579f1")
elseif(IDF_TARGET STREQUAL "esp32s3")
    set(_mesh_expected "dbba6dae63d27b95fa85d089d2e0c792f3156cf7b04299a667ea3549593a7173")
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
   NOT _mesh_internal_hash STREQUAL "19010cae1359dffcf2eb2f44690a83748f1c1f2d67cccfc1079fbc28992137ef" OR
   NOT _mesh_event_hash STREQUAL "0938301d1d1a7c102e25eba1cb411e430f3946c69b81802f4ff659da557ae756" OR
   NOT _mesh_header_hash STREQUAL "fc189e2698791888446427cef41f75f517f33c49e17aad688e32a3d540d24022")
    message(FATAL_ERROR "Unreviewed Mesh library, event boundary or public ABI")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_mesh_library}" "${_mesh_dir}/src/mesh_event.c" "${_mesh_dir}/include/esp_mesh.h" "${_mesh_dir}/include/esp_mesh_internal.h")
idf_component_get_property(_mesh_framework_lib esp32_mquickjs COMPONENT_LIB)
target_link_libraries(${_mesh_framework_lib} INTERFACE "-Wl,--wrap=esp_mesh_send_event_internal")
