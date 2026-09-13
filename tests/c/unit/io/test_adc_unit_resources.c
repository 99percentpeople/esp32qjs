#include "esp32_mquickjs_adc_unit_resources.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    FIXTURE_CREATE_ERROR = 701,
    FIXTURE_DELETE_CALIBRATION_ERROR = 702,
    FIXTURE_DELETE_UNIT_ERROR = 703,
};

static void *const FIXTURE_UNIT = (void *)(uintptr_t)0x11U;
static void *const FIXTURE_CALI_0 = (void *)(uintptr_t)0x21U;
static void *const FIXTURE_CALI_1 = (void *)(uintptr_t)0x22U;

typedef struct {
    bool fail_create;
    bool return_partial_unit;
    bool omit_unit;
    int fail_calibration_scheme;
    bool fail_delete_unit;
    char log[32];
    size_t log_length;
} fixture_t;

static int fixture_create(void *opaque, void **out_unit)
{
    fixture_t *fixture = opaque;

    fixture->log[fixture->log_length++] = 'C';
    if ((!fixture->fail_create || fixture->return_partial_unit) &&
        !fixture->omit_unit) {
        *out_unit = FIXTURE_UNIT;
    }
    return fixture->fail_create ? FIXTURE_CREATE_ERROR : 0;
}

static int fixture_delete_calibration(void *calibration, int scheme,
                                      void *opaque)
{
    fixture_t *fixture = opaque;

    if (calibration == FIXTURE_CALI_0) {
        fixture->log[fixture->log_length++] = 'a';
        assert(scheme == 1);
    } else {
        assert(calibration == FIXTURE_CALI_1);
        fixture->log[fixture->log_length++] = 'b';
        assert(scheme == 2);
    }
    return scheme == fixture->fail_calibration_scheme
               ? FIXTURE_DELETE_CALIBRATION_ERROR
               : 0;
}

static int fixture_delete_unit(void *unit, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(unit == FIXTURE_UNIT);
    fixture->log[fixture->log_length++] = 'U';
    return fixture->fail_delete_unit ? FIXTURE_DELETE_UNIT_ERROR : 0;
}

static esp32_mquickjs_adc_unit_resource_ops_t fixture_ops(fixture_t *fixture)
{
    return (esp32_mquickjs_adc_unit_resource_ops_t){
        .create_unit = fixture_create,
        .delete_calibration = fixture_delete_calibration,
        .delete_unit = fixture_delete_unit,
        .opaque = fixture,
    };
}

static esp32_mquickjs_adc_unit_resources_t fixture_resources(
    esp32_mquickjs_adc_calibration_resource_t *calibrations, size_t count)
{
    return (esp32_mquickjs_adc_unit_resources_t){
        .calibrations = calibrations,
        .calibration_count = count,
    };
}

static void test_creation_failures_are_rolled_back(void)
{
    fixture_t no_unit_fixture = {.fail_create = true};
    fixture_t partial_fixture = {
        .fail_create = true,
        .return_partial_unit = true,
    };
    fixture_t missing_fixture = {.omit_unit = true};
    esp32_mquickjs_adc_unit_resources_t resources = fixture_resources(NULL, 0);
    esp32_mquickjs_adc_unit_resource_ops_t ops =
        fixture_ops(&no_unit_fixture);

    assert(esp32_mquickjs_adc_unit_resources_init(
               &resources, &ops) == FIXTURE_CREATE_ERROR);
    assert(strcmp(no_unit_fixture.log, "C") == 0);
    assert(resources.unit == NULL);

    ops = fixture_ops(&partial_fixture);
    assert(esp32_mquickjs_adc_unit_resources_init(
               &resources, &ops) == FIXTURE_CREATE_ERROR);
    assert(strcmp(partial_fixture.log, "CU") == 0);
    assert(resources.unit == NULL);

    ops = fixture_ops(&missing_fixture);
    assert(esp32_mquickjs_adc_unit_resources_init(
               &resources, &ops) != 0);
    assert(strcmp(missing_fixture.log, "C") == 0);
    assert(resources.unit == NULL);
}

static void test_successful_unit_teardown_is_idempotent(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_adc_unit_resources_t resources = fixture_resources(NULL, 0);
    esp32_mquickjs_adc_unit_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_adc_unit_resources_init(
               &resources, &ops) == 0);
    assert(resources.unit == FIXTURE_UNIT);
    assert(esp32_mquickjs_adc_unit_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "CU") == 0);
    assert(resources.unit == NULL);
    assert(esp32_mquickjs_adc_unit_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "CU") == 0);
}

static void test_calibrations_are_deleted_before_the_unit(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_adc_calibration_resource_t calibrations[] = {
        {.handle = FIXTURE_CALI_0, .scheme = 1},
        {.handle = FIXTURE_CALI_1, .scheme = 2},
    };
    esp32_mquickjs_adc_unit_resources_t resources =
        fixture_resources(calibrations, 2);
    esp32_mquickjs_adc_unit_resource_ops_t ops = fixture_ops(&fixture);

    resources.unit = FIXTURE_UNIT;
    assert(esp32_mquickjs_adc_unit_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "abU") == 0);
    assert(calibrations[0].handle == NULL);
    assert(calibrations[0].scheme == 0);
    assert(calibrations[1].handle == NULL);
    assert(calibrations[1].scheme == 0);
    assert(resources.unit == NULL);
}

static void test_calibration_failure_retains_exact_suffix_for_retry(void)
{
    fixture_t fixture = {.fail_calibration_scheme = 2};
    esp32_mquickjs_adc_calibration_resource_t calibrations[] = {
        {.handle = FIXTURE_CALI_0, .scheme = 1},
        {.handle = FIXTURE_CALI_1, .scheme = 2},
    };
    esp32_mquickjs_adc_unit_resources_t resources =
        fixture_resources(calibrations, 2);
    esp32_mquickjs_adc_unit_resource_ops_t ops = fixture_ops(&fixture);

    resources.unit = FIXTURE_UNIT;
    assert(esp32_mquickjs_adc_unit_resources_deinit(
               &resources, &ops) == FIXTURE_DELETE_CALIBRATION_ERROR);
    assert(strcmp(fixture.log, "ab") == 0);
    assert(calibrations[0].handle == NULL);
    assert(calibrations[1].handle == FIXTURE_CALI_1);
    assert(resources.unit == FIXTURE_UNIT);

    fixture.fail_calibration_scheme = 0;
    assert(esp32_mquickjs_adc_unit_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "abbU") == 0);
}

static void test_unit_delete_failure_retains_unit_for_retry(void)
{
    fixture_t fixture = {.fail_delete_unit = true};
    esp32_mquickjs_adc_unit_resources_t resources = fixture_resources(NULL, 0);
    esp32_mquickjs_adc_unit_resource_ops_t ops = fixture_ops(&fixture);

    resources.unit = FIXTURE_UNIT;
    assert(esp32_mquickjs_adc_unit_resources_deinit(
               &resources, &ops) == FIXTURE_DELETE_UNIT_ERROR);
    assert(resources.unit == FIXTURE_UNIT);
    fixture.fail_delete_unit = false;
    assert(esp32_mquickjs_adc_unit_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "UU") == 0);
}

static void test_failed_init_prefers_cleanup_error_and_retains_unit(void)
{
    fixture_t fixture = {
        .fail_create = true,
        .return_partial_unit = true,
        .fail_delete_unit = true,
    };
    esp32_mquickjs_adc_unit_resources_t resources = fixture_resources(NULL, 0);
    esp32_mquickjs_adc_unit_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_adc_unit_resources_init(
               &resources, &ops) == FIXTURE_DELETE_UNIT_ERROR);
    assert(strcmp(fixture.log, "CU") == 0);
    assert(resources.unit == FIXTURE_UNIT);
    fixture.fail_delete_unit = false;
    assert(esp32_mquickjs_adc_unit_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "CUU") == 0);
}

int main(void)
{
    test_creation_failures_are_rolled_back();
    test_successful_unit_teardown_is_idempotent();
    test_calibrations_are_deleted_before_the_unit();
    test_calibration_failure_retains_exact_suffix_for_retry();
    test_unit_delete_failure_retains_unit_for_retry();
    test_failed_init_prefers_cleanup_error_and_retains_unit();
    return 0;
}
