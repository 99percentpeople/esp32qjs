/* Included by the pinned nan_pairing.c. Only the Wi-Fi task reads/writes this
 * binding. The Radio retains the service until explicit pairing close; native
 * service IDs, peer addresses and credential-array slots cannot grant a later
 * operation the authority of this one. Credentials are copied before PASN and
 * scrubbed only after its pending callbacks and TX owners have retired. */
#include "esp_timer.h"
extern bool current_task_is_wifi_task(void);
typedef struct {
    esp32_mquickjs_wifi_nan_pairing_config_t config;
    uint32_t identity;
    uint32_t committed_credential_id;
    int64_t credential_expires_us;
    uint8_t service_hash[6];
    wifi_nan_peer_creds_t credential;
} esp32qjs_pairing_binding_t;
static esp32qjs_pairing_binding_t *s_esp32qjs_pairing_binding;
static struct peer_svc_info *nan_find_peer_svc_exact(uint8_t own_svc_id,
    uint8_t peer_svc_id, const uint8_t *peer_mac);

static void esp32qjs_pairing_clear_nik(uint8_t (*value)[NAN_APP_PEER_NIK_LEN])
{ forced_memzero(*value, sizeof(*value)); }
static void esp32qjs_pairing_clear_npk(uint8_t (*value)[ESP_WIFI_NAN_NPK_LEN])
{ forced_memzero(*value, sizeof(*value)); }
static void esp32qjs_pairing_clear_plain(uint8_t (*value)[64])
{ forced_memzero(*value, sizeof(*value)); }

bool esp32_mquickjs_wifi_nan_pairing_services(const uint8_t peer[6],
    uint8_t *service_id, uint8_t *peer_service_id)
{
    const esp32qjs_pairing_binding_t *binding = s_esp32qjs_pairing_binding;
    if (!current_task_is_wifi_task() || !binding || !peer ||
        memcmp(peer, binding->config.peer, 6)) return false;
    if (service_id) *service_id = binding->config.service_id;
    if (peer_service_id) *peer_service_id = binding->config.peer_service_id;
    return true;
}

void esp32_mquickjs_wifi_nan_pairing_release_binding(void)
{
    if (!current_task_is_wifi_task()) return;
    esp32qjs_pairing_binding_t *binding = s_esp32qjs_pairing_binding;
    if (!binding) return;
    esp32_mquickjs_wifi_nan_pasn_status_t status;
    esp32_mquickjs_wifi_nan_pasn_status(&status);
    if (binding->identity && status.identity == binding->identity && (!status.paired || status.error))
        nan_app_remove_paired_peer(binding->config.peer);
    NAN_DATA_LOCK();
    struct own_svc_info *own = nan_find_own_svc(binding->config.service_id);
    if (own && !memcmp(own->svc_hash, binding->service_hash, 6) &&
        MACADDR_EQUAL(own->verify_session_peer_nmi, binding->config.peer)) {
        own->verify_session_pending = false;
        forced_memzero(own->verify_session_peer_nmi, sizeof(own->verify_session_peer_nmi));
    }
    NAN_DATA_UNLOCK();
    s_esp32qjs_pairing_binding = NULL;
    forced_memzero(binding, sizeof(*binding));
    esp32_mquickjs_memory_payload_free(binding);
}

static int esp32qjs_pairing_service_idle_command(void *opaque, void *unused)
{
    (void)unused;
    uint8_t id = *(uint8_t *)opaque;
    return s_esp32qjs_pairing_binding && s_esp32qjs_pairing_binding->config.service_id == id
        ? ESP_ERR_INVALID_STATE : ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_nan_pairing_service_idle(uint8_t service_id)
{
    if (!service_id) return ESP_ERR_INVALID_ARG;
    return current_task_is_wifi_task() ? esp32qjs_pairing_service_idle_command(&service_id, NULL) :
        eloop_register_timeout_blocking(esp32qjs_pairing_service_idle_command, &service_id, NULL);
}
