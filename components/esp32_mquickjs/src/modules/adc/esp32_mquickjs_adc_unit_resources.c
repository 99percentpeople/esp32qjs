#include "esp32_mquickjs_adc_unit_resources.h"

#include <stdbool.h>
#include <stddef.h>

#define ADC_UNIT_RESOURCES_INVALID (-1)

static bool adc_unit_resources_valid(
    const esp32_mquickjs_adc_unit_resources_t *resources,
    const esp32_mquickjs_adc_unit_resource_ops_t *ops)
{
    return resources != NULL && ops != NULL &&
           ops->delete_calibration != NULL && ops->delete_unit != NULL &&
           (resources->calibration_count == 0 ||
            resources->calibrations != NULL);
}

int esp32_mquickjs_adc_unit_resources_deinit(
    esp32_mquickjs_adc_unit_resources_t *resources,
    const esp32_mquickjs_adc_unit_resource_ops_t *ops)
{
    size_t index;
    int result;

    if (!adc_unit_resources_valid(resources, ops)) {
        return ADC_UNIT_RESOURCES_INVALID;
    }
    for (index = 0; index < resources->calibration_count; ++index) {
        esp32_mquickjs_adc_calibration_resource_t *calibration =
            &resources->calibrations[index];

        if (calibration->handle == NULL) {
            continue;
        }
        result = ops->delete_calibration(
            calibration->handle, calibration->scheme, ops->opaque);
        if (result != 0) {
            return result;
        }
        calibration->handle = NULL;
        calibration->scheme = 0;
    }
    if (resources->unit != NULL) {
        result = ops->delete_unit(resources->unit, ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->unit = NULL;
    }
    return 0;
}

int esp32_mquickjs_adc_unit_resources_init(
    esp32_mquickjs_adc_unit_resources_t *resources,
    const esp32_mquickjs_adc_unit_resource_ops_t *ops)
{
    int result;
    int cleanup_result;

    if (!adc_unit_resources_valid(resources, ops) ||
        ops->create_unit == NULL) {
        return ADC_UNIT_RESOURCES_INVALID;
    }
    resources->unit = NULL;
    result = ops->create_unit(ops->opaque, &resources->unit);
    if (result == 0 && resources->unit == NULL) {
        result = ADC_UNIT_RESOURCES_INVALID;
    }
    if (result == 0) {
        return 0;
    }
    cleanup_result = esp32_mquickjs_adc_unit_resources_deinit(resources, ops);
    return cleanup_result != 0 ? cleanup_result : result;
}
