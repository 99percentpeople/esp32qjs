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
set(_esp32qjs_camera_driver
    "${ESP32QJS_CAMERA_COMPONENT_DIR}/driver/esp_camera.c")

if(NOT EXISTS "${_esp32qjs_camera_manifest}" OR
   NOT EXISTS "${_esp32qjs_camera_hal}" OR
   NOT EXISTS "${_esp32qjs_camera_driver}")
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

# esp32-camera 2.1.7 permits every sensor implementation to be disabled, but
# its unsigned probe loop then compares against a constant zero array length.
# GCC diagnoses that valid zero-sensor configuration under -Wtype-limits, and
# ESP-IDF promotes the warning to an error. Guard the loop without selecting a
# fake sensor or exposing the camera feature on boards that do not have one.
file(READ "${_esp32qjs_camera_driver}" _esp32qjs_camera_driver_text)
set(_esp32qjs_camera_zero_sensor_marker
    "ESP32QJS zero-sensor build guard")
string(FIND "${_esp32qjs_camera_driver_text}"
    "${_esp32qjs_camera_zero_sensor_marker}"
    _esp32qjs_camera_zero_sensor_patch_position)

if(_esp32qjs_camera_zero_sensor_patch_position EQUAL -1)
    set(_esp32qjs_camera_probe_before [=[
        for (size_t i = 0; i < sizeof(g_sensors) / sizeof(sensor_func_t); i++) {
            if (g_sensors[i].detect(slv_addr, id)) {
                ESP_LOGI(TAG, "Camera PID=0x%02x VER=0x%02x MIDL=0x%02x MIDH=0x%02x",
                    id->PID, id->VER, id->MIDH, id->MIDL);
                camera_sensor_info_t *info = esp_camera_sensor_get_info(id);
                if (NULL != info) {
                    *out_camera_model = info->model;
                    ESP_LOGI(TAG, "Detected %s camera", info->name);
                    g_sensors[i].init(&s_state->sensor);
                    break;
                }
            }
        }
]=])
    set(_esp32qjs_camera_probe_after [=[
#if CONFIG_OV2640_SUPPORT || CONFIG_OV3660_SUPPORT || \
    CONFIG_OV7670_SUPPORT || CONFIG_OV7725_SUPPORT || \
    CONFIG_NT99141_SUPPORT || CONFIG_OV5640_SUPPORT || \
    CONFIG_GC2145_SUPPORT || CONFIG_GC032A_SUPPORT || \
    CONFIG_GC0308_SUPPORT || CONFIG_BF3005_SUPPORT || \
    CONFIG_BF20A6_SUPPORT || CONFIG_SC101IOT_SUPPORT || \
    CONFIG_SC030IOT_SUPPORT || CONFIG_SC031GS_SUPPORT || \
    CONFIG_HM1055_SUPPORT || CONFIG_HM0360_SUPPORT || \
    CONFIG_MEGA_CCM_SUPPORT
        /* ESP32QJS zero-sensor build guard: skip the constant-zero loop. */
        for (size_t i = 0; i < sizeof(g_sensors) / sizeof(sensor_func_t); i++) {
            if (g_sensors[i].detect(slv_addr, id)) {
                ESP_LOGI(TAG, "Camera PID=0x%02x VER=0x%02x MIDL=0x%02x MIDH=0x%02x",
                    id->PID, id->VER, id->MIDH, id->MIDL);
                camera_sensor_info_t *info = esp_camera_sensor_get_info(id);
                if (NULL != info) {
                    *out_camera_model = info->model;
                    ESP_LOGI(TAG, "Detected %s camera", info->name);
                    g_sensors[i].init(&s_state->sensor);
                    break;
                }
            }
        }
#endif /* ESP32QJS zero-sensor build guard */
]=])
    string(FIND "${_esp32qjs_camera_driver_text}"
        "${_esp32qjs_camera_probe_before}" _esp32qjs_camera_probe_site)
    if(_esp32qjs_camera_probe_site EQUAL -1)
        message(FATAL_ERROR
            "esp32-camera 2.1.7 esp_camera.c no longer matches the expected zero-sensor patch site")
    endif()

    string(REPLACE
        "${_esp32qjs_camera_probe_before}"
        "${_esp32qjs_camera_probe_after}"
        _esp32qjs_camera_driver_patched
        "${_esp32qjs_camera_driver_text}")
    file(WRITE "${_esp32qjs_camera_driver}"
        "${_esp32qjs_camera_driver_patched}")
    message(STATUS
        "Applied esp32-camera 2.1.7 zero-sensor probe workaround")
endif()
