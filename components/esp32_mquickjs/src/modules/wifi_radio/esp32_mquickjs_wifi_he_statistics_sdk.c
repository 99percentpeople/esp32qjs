#include "esp32_mquickjs_wifi_he_statistics.h"
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
#include "esp_err.h"
#include "esp_wifi_he.h"
#include "esp_wifi_he_types.h"
#include "esp_private/esp_wifi_he_types_private.h"
#include "esp_private/wifi_os_adapter.h"
#include <stddef.h>
#include <string.h>
struct os_reltime;
#include "utils/eloop.h"
bool current_task_is_wifi_task(void);

/* Fixed libpp HAL boundary, called only from the Wi-Fi task's SDK ioctl.
 * The CMake gate pins both the allocator code and its ROM data declarations.
 * Native helpers continue to own allocation/collection; no JS/storage pointers
 * cross this boundary and all frees use the original SDK allocator hook. */
esp_err_t esp_test_enable_rx_statistics(void);
void esp_test_disable_rx_statistics(void);
esp_err_t esp_test_enable_rx_mu_statistics(void);
void esp_test_disable_rx_mu_statistics(void);
esp_err_t esp_test_enable_tx_statistics(esp_wifi_aci_t aci);
void esp_test_disable_tx_statistics(esp_wifi_aci_t aci);
extern esp_test_tx_statistics_t *esp_test_tx_statistics[ESP_WIFI_ACI_MAX];
extern esp_test_tx_tb_statistics_t *esp_test_tx_tb_statistics[ESP_WIFI_ACI_MAX];
extern esp_test_tx_fail_statistics_t *esp_test_tx_fail_statistics[ESP_WIFI_ACI_MAX][6];
extern esp_test_rx_statistics_t *esp_test_rx_statistics[2];
extern esp_test_rx_error_occurs_t *esp_test_rx_error_occurs;
extern esp_test_rx_mu_statistics_t *esp_test_rx_mu_statistics;
extern uint8_t esp_test_tx_statistics_aci_bitmap;
_Static_assert(ESP_WIFI_ACI_MAX == 4 && sizeof(void *) == 4, "Review fixed statistics ROM pointer tables");
_Static_assert(sizeof(esp_test_tx_statistics_t) == 136 && sizeof(esp_test_tx_tb_statistics_t) == 32 &&
    sizeof(esp_test_tx_fail_statistics_t) == 164, "Review fixed SDK statistics allocations");

esp_err_t __wrap_hal_enable_rx_statistics(bool ordinary, bool multi_user)
{
    /* The original ordinary enable overwrites existing allocation pointers.
     * Replace the complete pair, then preserve the FIRST allocation error:
     * the original HAL overwrites it with MU success and ultimately returns 0. */
    esp_test_disable_rx_statistics();
    esp_test_disable_rx_mu_statistics();
    esp_err_t error = ordinary ? esp_test_enable_rx_statistics() : ESP_OK;
    if (error == ESP_OK && multi_user) error = esp_test_enable_rx_mu_statistics();
    if (error != ESP_OK) {
        esp_test_disable_rx_statistics();
        esp_test_disable_rx_mu_statistics();
    }
    return error;
}

esp_err_t __wrap_hal_enable_tx_statistics(esp_wifi_aci_t aci, bool enabled)
{
    if ((unsigned)aci >= ESP_WIFI_ACI_MAX) return ESP_ERR_INVALID_ARG;
    if (!enabled) {
        esp_test_disable_tx_statistics(aci);
        return ESP_OK;
    }
    esp_err_t error = esp_test_enable_tx_statistics(aci);
    if (error != ESP_OK) {
        /* Native enable sets its ACI bit only after all three allocations.
         * Its error cleanup tests that bit first, so partial allocations leak.
         * Here the bit is still clear: free the three owned bases directly;
         * the other five failure-stat entries are interior aliases, not owners. */
        if (esp_test_tx_statistics[aci] != NULL) {
            g_wifi_osi_funcs._free(esp_test_tx_statistics[aci]);
            esp_test_tx_statistics[aci] = NULL;
        }
        if (esp_test_tx_tb_statistics[aci] != NULL) {
            g_wifi_osi_funcs._free(esp_test_tx_tb_statistics[aci]);
            esp_test_tx_tb_statistics[aci] = NULL;
        }
        if (esp_test_tx_fail_statistics[aci][0] != NULL)
            g_wifi_osi_funcs._free(esp_test_tx_fail_statistics[aci][0]);
        memset(esp_test_tx_fail_statistics[aci], 0, sizeof(esp_test_tx_fail_statistics[aci]));
    }
    return error;
}

typedef struct {
    esp32_mquickjs_wifi_he_statistics_t actual;
    bool retire, entered;
    esp_err_t error;
} he_statistics_command_t;

static int he_statistics_snapshot_dispatch(void *opaque, void *unused)
{
    (void)unused;
    he_statistics_command_t *command = opaque;
    command->entered = true;
    command->error = ESP_ERR_INVALID_RESPONSE;
    bool ordinary = esp_test_rx_statistics[0] != NULL;
    if (ordinary != (esp_test_rx_statistics[1] != NULL) || ordinary != (esp_test_rx_error_occurs != NULL) ||
        (esp_test_tx_statistics_aci_bitmap & ~15U)) return command->error;
    for (unsigned aci = 0; aci < 4; ++aci) {
        bool enabled = (esp_test_tx_statistics_aci_bitmap & (1U << aci)) != 0;
        if (enabled != (esp_test_tx_statistics[aci] != NULL) ||
            enabled != (esp_test_tx_tb_statistics[aci] != NULL) ||
            enabled != (esp_test_tx_fail_statistics[aci][0] != NULL)) return command->error;
    }
    command->actual = (esp32_mquickjs_wifi_he_statistics_t){ordinary,
        esp_test_rx_mu_statistics != NULL, esp_test_tx_statistics_aci_bitmap};
    if (command->retire) {
        /* No allocation and no command recursion inside the Wi-Fi task.
         * Native disable zeroes all owning pointers; interior aliases have no
         * independent lifetime and are never freed separately. */
        esp_test_disable_rx_statistics();
        esp_test_disable_rx_mu_statistics();
        for (unsigned aci = 0; aci < 4; ++aci) esp_test_disable_tx_statistics(aci);
    }
    command->error = ESP_OK;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_he_statistics_snapshot(esp32_mquickjs_wifi_he_statistics_t *actual, bool retire)
{
    if (!actual) return ESP_ERR_INVALID_ARG;
    *actual = (esp32_mquickjs_wifi_he_statistics_t){0};
    he_statistics_command_t command = {.retire = retire, .error = ESP_ERR_INVALID_STATE};
    /* Fixed eloop holds stack storage until completion or confirmed discard.
     * Failure to enter is not observation or retirement proof. */
    int error = current_task_is_wifi_task() ? he_statistics_snapshot_dispatch(&command, NULL) :
        eloop_register_timeout_blocking(he_statistics_snapshot_dispatch, &command, NULL);
    if (!command.entered) return ESP_FAIL;
    if (command.error != ESP_OK) return command.error;
    if (error != ESP_OK) return ESP_FAIL;
    *actual = command.actual;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_he_statistics_restore(const esp32_mquickjs_wifi_he_statistics_t *wanted,
    uint8_t *completed)
{
    if (!wanted || !completed || (wanted->tx_mask & ~15U) || *completed > 5) return ESP_ERR_INVALID_ARG;
    while (*completed < 5) {
        unsigned phase = *completed;
        esp_err_t error = phase == 0 ? esp_wifi_enable_rx_statistics(wanted->ordinary, wanted->multi_user) :
            esp_wifi_enable_tx_statistics((esp_wifi_aci_t)(phase - 1), (wanted->tx_mask & (1U << (phase - 1))) != 0);
        if (error != ESP_OK) return error;
        ++*completed;
    }
    esp32_mquickjs_wifi_he_statistics_t actual;
    esp_err_t error = esp32_mquickjs_wifi_he_statistics_snapshot(&actual, false);
    return error == ESP_OK && !wifi_he_statistics_equal(wanted, &actual) ? ESP_ERR_INVALID_RESPONSE : error;
}
#endif
