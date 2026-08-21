# Work around espressif/esp32-camera#853 for the exact dependency version used
# by ESP32QJS. OV3660 may pad JPEG frames by more than one DMA node after EOI,
# so the v2.1.7 tail-only probe rejects valid frames when PSRAM DMA is enabled.
#
# This runs after project(), when IDF Component Manager has downloaded the
# component. It deliberately fails closed if the pinned upstream source changes.

if(NOT IDF_TARGET STREQUAL "esp32s3")
    return()
endif()

if(NOT DEFINED ESP32QJS_CAMERA_COMPONENT_DIR)
    set(ESP32QJS_CAMERA_COMPONENT_DIR
        "${CMAKE_SOURCE_DIR}/managed_components/espressif__esp32-camera")
endif()

set(_esp32qjs_camera_manifest
    "${ESP32QJS_CAMERA_COMPONENT_DIR}/idf_component.yml")
set(_esp32qjs_camera_hal
    "${ESP32QJS_CAMERA_COMPONENT_DIR}/driver/cam_hal.c")

if(NOT EXISTS "${_esp32qjs_camera_manifest}" OR
   NOT EXISTS "${_esp32qjs_camera_hal}")
    message(FATAL_ERROR
        "ESP32QJS camera patch requires the managed esp32-camera component at "
        "${ESP32QJS_CAMERA_COMPONENT_DIR}")
endif()

file(READ "${_esp32qjs_camera_manifest}" _esp32qjs_camera_manifest_text)
string(REGEX MATCH "version:[ \t]+['\"]?2\\.1\\.7['\"]?"
    _esp32qjs_camera_version_match "${_esp32qjs_camera_manifest_text}")
if(NOT _esp32qjs_camera_version_match)
    message(FATAL_ERROR
        "ESP32QJS camera workaround only supports esp32-camera 2.1.7; "
        "review and remove or update the patch before changing the dependency")
endif()

file(READ "${_esp32qjs_camera_hal}" _esp32qjs_camera_hal_text)
set(_esp32qjs_camera_patch_marker
    "ESP32QJS workaround for espressif/esp32-camera#853")
string(FIND "${_esp32qjs_camera_hal_text}"
    "${_esp32qjs_camera_patch_marker}" _esp32qjs_camera_patch_position)

if(_esp32qjs_camera_patch_position EQUAL -1)
    set(_esp32qjs_camera_patch_before [=[
            if (offset_e >= 0) {
]=])
    set(_esp32qjs_camera_patch_after [=[
            /* ESP32QJS workaround for espressif/esp32-camera#853: OV3660 can
             * append more than one DMA node of padding after JPEG EOI. If the
             * cheap tail probe misses, invalidate and search the whole frame. */
            if (offset_e < 0 && cam_obj->psram_mode) {
                cam_drop_psram_cache(dma_buffer->buf, dma_buffer->len);
                offset_e = cam_verify_jpeg_eoi(dma_buffer->buf,
                                               dma_buffer->len,
                                               false);
            }

            if (offset_e >= 0) {
]=])
    string(FIND "${_esp32qjs_camera_hal_text}"
        "${_esp32qjs_camera_patch_before}" _esp32qjs_camera_patch_site)
    if(_esp32qjs_camera_patch_site EQUAL -1)
        message(FATAL_ERROR
            "esp32-camera 2.1.7 cam_hal.c no longer matches the expected patch site")
    endif()

    string(REPLACE
        "${_esp32qjs_camera_patch_before}"
        "${_esp32qjs_camera_patch_after}"
        _esp32qjs_camera_hal_patched
        "${_esp32qjs_camera_hal_text}")
    file(WRITE "${_esp32qjs_camera_hal}" "${_esp32qjs_camera_hal_patched}")
    message(STATUS
        "Applied esp32-camera 2.1.7 OV3660 PSRAM-DMA JPEG EOI workaround")
endif()
