#pragma once

#include <stddef.h>

typedef int (*esp32_mquickjs_adc_unit_create_fn)(
    void *opaque, void **out_unit);
typedef int (*esp32_mquickjs_adc_calibration_delete_fn)(
    void *calibration, int scheme, void *opaque);
typedef int (*esp32_mquickjs_adc_unit_delete_fn)(
    void *unit, void *opaque);

typedef struct {
    esp32_mquickjs_adc_unit_create_fn create_unit;
    esp32_mquickjs_adc_calibration_delete_fn delete_calibration;
    esp32_mquickjs_adc_unit_delete_fn delete_unit;
    void *opaque;
} esp32_mquickjs_adc_unit_resource_ops_t;

typedef struct {
    void *handle;
    int scheme;
} esp32_mquickjs_adc_calibration_resource_t;

typedef struct {
    void *unit;
    esp32_mquickjs_adc_calibration_resource_t *calibrations;
    size_t calibration_count;
} esp32_mquickjs_adc_unit_resources_t;

int esp32_mquickjs_adc_unit_resources_init(
    esp32_mquickjs_adc_unit_resources_t *resources,
    const esp32_mquickjs_adc_unit_resource_ops_t *ops);

int esp32_mquickjs_adc_unit_resources_deinit(
    esp32_mquickjs_adc_unit_resources_t *resources,
    const esp32_mquickjs_adc_unit_resource_ops_t *ops);
