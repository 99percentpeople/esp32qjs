"""Reviewed camera replacement fragments; ordered within each source file."""

PATCH_BEFORE = r'''            if (offset_e >= 0) {
'''

PATCH_AFTER = r'''            /* ESP32QJS workaround for espressif/esp32-camera#853: OV3660 can
             * append more than one DMA node of padding after JPEG EOI. If the
             * cheap tail probe misses, invalidate and search the whole frame. */
            if (offset_e < 0 && cam_obj->psram_mode) {
                cam_drop_psram_cache(dma_buffer->buf, dma_buffer->len);
                offset_e = cam_verify_jpeg_eoi(dma_buffer->buf,
                                               dma_buffer->len,
                                               false);
            }

            if (offset_e >= 0) {
'''

PROBE_BEFORE = r'''        for (size_t i = 0; i < sizeof(g_sensors) / sizeof(sensor_func_t); i++) {
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
'''

PROBE_AFTER = r'''#if CONFIG_OV2640_SUPPORT || CONFIG_OV3660_SUPPORT || \
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
'''

SENSOR_TABLE_START_BEFORE = r'''typedef struct {
    int (*detect)(int slv_addr, sensor_id_t *id);
    int (*init)(sensor_t *sensor);
} sensor_func_t;
'''

SENSOR_TABLE_START_AFTER = r'''/* ESP32QJS camera sensor support predicate. */
#define ESP32QJS_CAMERA_SENSOR_SUPPORT_ENABLED ( \
    CONFIG_OV2640_SUPPORT || CONFIG_OV3660_SUPPORT || \
    CONFIG_OV7670_SUPPORT || CONFIG_OV7725_SUPPORT || \
    CONFIG_NT99141_SUPPORT || CONFIG_OV5640_SUPPORT || \
    CONFIG_GC2145_SUPPORT || CONFIG_GC032A_SUPPORT || \
    CONFIG_GC0308_SUPPORT || CONFIG_BF3005_SUPPORT || \
    CONFIG_BF20A6_SUPPORT || CONFIG_SC101IOT_SUPPORT || \
    CONFIG_SC030IOT_SUPPORT || CONFIG_SC031GS_SUPPORT || \
    CONFIG_HM1055_SUPPORT || CONFIG_HM0360_SUPPORT || \
    CONFIG_MEGA_CCM_SUPPORT)

#if ESP32QJS_CAMERA_SENSOR_SUPPORT_ENABLED
typedef struct {
    int (*detect)(int slv_addr, sensor_id_t *id);
    int (*init)(sensor_t *sensor);
} sensor_func_t;
'''

SENSOR_TABLE_END_BEFORE = r'''#if CONFIG_HM0360_SUPPORT
    {esp32_camera_hm0360_detect, esp32_camera_hm0360_init},
#endif
};
'''

SENSOR_TABLE_END_AFTER = r'''#if CONFIG_HM0360_SUPPORT
    {esp32_camera_hm0360_detect, esp32_camera_hm0360_init},
#endif
};
#endif /* ESP32QJS_CAMERA_SENSOR_SUPPORT_ENABLED */
'''

PROBE_GUARD_BEFORE = r'''        sensor_id_t *id = &s_state->sensor.id;

#if CONFIG_OV2640_SUPPORT || CONFIG_OV3660_SUPPORT || \
    CONFIG_OV7670_SUPPORT || CONFIG_OV7725_SUPPORT || \
    CONFIG_NT99141_SUPPORT || CONFIG_OV5640_SUPPORT || \
    CONFIG_GC2145_SUPPORT || CONFIG_GC032A_SUPPORT || \
    CONFIG_GC0308_SUPPORT || CONFIG_BF3005_SUPPORT || \
    CONFIG_BF20A6_SUPPORT || CONFIG_SC101IOT_SUPPORT || \
    CONFIG_SC030IOT_SUPPORT || CONFIG_SC031GS_SUPPORT || \
    CONFIG_HM1055_SUPPORT || CONFIG_HM0360_SUPPORT || \
    CONFIG_MEGA_CCM_SUPPORT
'''

PROBE_GUARD_AFTER = r'''#if ESP32QJS_CAMERA_SENSOR_SUPPORT_ENABLED
        sensor_id_t *id = &s_state->sensor.id;
'''
