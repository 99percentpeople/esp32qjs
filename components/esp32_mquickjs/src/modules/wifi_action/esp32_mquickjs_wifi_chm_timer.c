#include "esp32_mquickjs_wifi_chm_timer.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_DPP_SUPPORT || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_private/wifi_os_adapter.h"
#include "freertos/FreeRTOS.h"
#include <limits.h>
#include <string.h>

/* fff9895c82 wl_chm.o: C5 adds one word to the channel operation record.
 * The parent archive hash and per-member patch gate cover these offsets and
 * all native calls, including same-object end/reset calls that --wrap misses. */
#if CONFIG_IDF_TARGET_ESP32C5
#define CHM_TIMER_OFFSET 40U
#define CHM_RECORD_SIZE 28U
#else
#define CHM_TIMER_OFFSET 36U
#define CHM_RECORD_SIZE 24U
#endif
_Static_assert(sizeof(ETSTimer) == 20 && offsetof(ETSTimer, timer_arg) == 16,
    "reviewed CHM legacy timer layout");
_Static_assert(sizeof(uintptr_t) == 4 && offsetof(wifi_osi_funcs_t, _timer_arm) == 224 &&
    offsetof(wifi_osi_funcs_t, _timer_disarm) == 228 && offsetof(wifi_osi_funcs_t, _timer_done) == 232 &&
    offsetof(wifi_osi_funcs_t, _timer_setfn) == 236 && offsetof(wifi_osi_funcs_t, _timer_arm_us) == 240,
    "reviewed CHM native timer dispatch");
extern uint8_t *g_chm;
int ieee80211_timer_process(int signal, int operation, void *argument);
void __real_chm_end_op_timeout_process(void *argument);
int __real_chm_start_op(void *channel, uint32_t minimum_ms, uint32_t maximum_ms,
    void (*start)(void *, int), void (*end)(void *, int), void *context);

typedef struct {
    esp_timer_handle_t handle;
    uint64_t deadline;
    uint32_t identity;
    esp_err_t cleanup_error;
    bool posted;
} chm_timer_t;
static DRAM_ATTR chm_timer_t s_chm_timers[2];
static DRAM_ATTR esp32qjs_wifi_chm_timer_status_t s_chm_status = {.last_identity = 1};
static DRAM_ATTR portMUX_TYPE s_chm_lock = portMUX_INITIALIZER_UNLOCKED;

static int IRAM_ATTR chm_timer_index(const ETSTimer *timer)
{
    uintptr_t base = (uintptr_t)g_chm;
    if (!base) return -1;
    if ((uintptr_t)timer == base + CHM_TIMER_OFFSET) return 0;
    if ((uintptr_t)timer == base + CHM_TIMER_OFFSET + sizeof(ETSTimer)) return 1;
    return -1;
}

static void IRAM_ATTR chm_invalidate_locked(unsigned slot)
{
    s_chm_timers[slot].identity = 0;
    s_chm_timers[slot].posted = false;
}

static void IRAM_ATTR chm_failed(esp_err_t error)
{
    portENTER_CRITICAL_SAFE(&s_chm_lock);
    if (s_chm_status.error == ESP_OK) s_chm_status.error = error;
    chm_invalidate_locked(0); chm_invalidate_locked(1);
    portEXIT_CRITICAL_SAFE(&s_chm_lock);
}

static void chm_timer_wake(void *argument)
{
    unsigned slot = (unsigned)(uintptr_t)argument;
    if (slot >= 2) return;
    uint64_t now = (uint64_t)esp_timer_get_time();
    portENTER_CRITICAL_SAFE(&s_chm_lock);
    chm_timer_t *timer = &s_chm_timers[slot];
    uint32_t identity = timer->identity;
    bool due = identity && s_chm_status.error == ESP_OK && !timer->posted && now >= timer->deadline;
    if (due) timer->posted = true;
    portEXIT_CRITICAL_SAFE(&s_chm_lock);
    if (!due) return;
    /* This callback carries only the permanent slot number. It is explicitly
     * a wake, not completion of the arm which scheduled it. A delayed wake
     * can observe a successor only after that successor's own deadline.
     * From here onward the immutable arm ticket crosses the native queue. */
    int error = ieee80211_timer_process(7, 8, (void *)(uintptr_t)identity);
    if (error) {
        portENTER_CRITICAL_SAFE(&s_chm_lock);
        if (s_chm_status.post_failures != UINT32_MAX) ++s_chm_status.post_failures;
        if (timer->identity == identity) timer->posted = false;
        portEXIT_CRITICAL_SAFE(&s_chm_lock);
        /* No rearm here: a timer-task retry could overwrite a newer native
         * arm. The existing DPP native poll services the retained deadline. */
    }
}

static esp_err_t IRAM_ATTR chm_timer_stop(unsigned slot)
{
    portENTER_CRITICAL_SAFE(&s_chm_lock);
    chm_invalidate_locked(slot);
    esp_timer_handle_t handle = s_chm_timers[slot].handle;
    portEXIT_CRITICAL_SAFE(&s_chm_lock);
    esp_err_t error = handle ? esp_timer_stop(handle) : ESP_OK;
    if (error == ESP_ERR_INVALID_STATE) error = ESP_OK; /* expired/unarmed */
    if (error != ESP_OK) chm_failed(error);
    return error;
}

static esp_err_t chm_timer_delete(unsigned slot)
{
    esp_err_t error = chm_timer_stop(slot);
    esp_timer_handle_t handle = s_chm_timers[slot].handle; /* native task only */
    if (error == ESP_OK && handle) error = esp_timer_delete(handle);
    portENTER_CRITICAL_SAFE(&s_chm_lock);
    s_chm_timers[slot].cleanup_error = error;
    if (error == ESP_OK) s_chm_timers[slot].handle = NULL;
    portEXIT_CRITICAL_SAFE(&s_chm_lock);
    if (error != ESP_OK) chm_failed(error);
    /* Failed deletion retains the handle outside g_chm, which the SDK may
     * free after its void done callback. A later done/setfn retries cleanup.
     * Numeric wake arguments never refer to that freed SDK allocation. */
    return error;
}

bool esp32qjs_wifi_chm_timer_setfn(ETSTimer *timer, ETSTimerFunc *callback, void *argument)
{
    int slot = chm_timer_index(timer);
    if (slot < 0) return false;
    if (chm_timer_delete((unsigned)slot) != ESP_OK) return true;
    memset(timer, 0, sizeof(*timer));
    timer->timer_expire = UINT32_C(0x12121212);
    if (!callback || (uintptr_t)argument != (uintptr_t)slot) {
        chm_failed(ESP_ERR_INVALID_STATE); return true;
    }
    if (s_chm_status.error != ESP_OK) return true;
    const esp_timer_create_args_t args = {.callback = chm_timer_wake,
        .arg = (void *)(uintptr_t)slot, .dispatch_method = ESP_TIMER_TASK, .name = "CHM deadline"};
    esp_timer_handle_t handle = NULL;
    esp_err_t error = esp_timer_create(&args, &handle);
    if (error != ESP_OK) { chm_failed(error); return true; }
    portENTER_CRITICAL_SAFE(&s_chm_lock);
    s_chm_timers[slot].handle = handle;
    portEXIT_CRITICAL_SAFE(&s_chm_lock);
    timer->timer_arg = handle;
    return true;
}

bool IRAM_ATTR esp32qjs_wifi_chm_timer_disarm(ETSTimer *timer)
{
    int slot = chm_timer_index(timer);
    if (slot < 0) return false;
    chm_timer_stop((unsigned)slot);
    return true;
}

bool esp32qjs_wifi_chm_timer_done(ETSTimer *timer)
{
    int slot = chm_timer_index(timer);
    if (slot < 0) return false;
    if (chm_timer_delete((unsigned)slot) == ESP_OK) {
        timer->timer_arg = NULL;
        timer->timer_expire = 0;
    }
    return true;
}

bool IRAM_ATTR esp32qjs_wifi_chm_timer_arm(ETSTimer *timer, uint64_t us, bool repeat)
{
    int slot = chm_timer_index(timer);
    if (slot < 0) return false;
    if (chm_timer_stop((unsigned)slot) != ESP_OK) return true;
    uint64_t now = (uint64_t)esp_timer_get_time();
    esp_timer_handle_t handle = s_chm_timers[slot].handle;
    esp_err_t error = ESP_OK;
    portENTER_CRITICAL_SAFE(&s_chm_lock);
    if (s_chm_status.error != ESP_OK) error = s_chm_status.error;
    else if (!handle || timer->timer_arg != handle) error = ESP_ERR_INVALID_STATE;
    else if (repeat || now > INT64_MAX || us > INT64_MAX - now) error = ESP_ERR_INVALID_ARG;
    else if (s_chm_status.last_identity == UINT32_MAX) error = ESP_ERR_NO_MEM;
    else {
        s_chm_timers[slot].deadline = now + us;
        s_chm_timers[slot].identity = ++s_chm_status.last_identity;
    }
    portEXIT_CRITICAL_SAFE(&s_chm_lock);
    if (error == ESP_OK) error = esp_timer_start_once(handle, us);
    if (error != ESP_OK) chm_failed(error);
    return true;
}

void *esp32qjs_wifi_chm_record_reset(void *destination, int value, size_t size)
{
    if (g_chm && destination == g_chm + 4 && size == CHM_RECORD_SIZE && !value) {
        portENTER_CRITICAL_SAFE(&s_chm_lock);
        chm_invalidate_locked(0); chm_invalidate_locked(1);
        portEXIT_CRITICAL_SAFE(&s_chm_lock);
    }
    return memset(destination, value, size);
}

int __wrap_chm_start_op(void *channel, uint32_t minimum_ms, uint32_t maximum_ms,
    void (*start)(void *, int), void (*end)(void *, int), void *context)
{
    /* Return the SDK's native rejection before taking channel/storage ownership.
     * Reserve capacity for both arms; never wrap into a live/old queue ticket.
     * No same-object start caller exists in the reviewed wl_chm.o. */
    portENTER_CRITICAL_SAFE(&s_chm_lock);
    if (s_chm_status.error == ESP_OK && s_chm_status.last_identity > UINT32_MAX - 2U)
        s_chm_status.error = ESP_ERR_NO_MEM;
    bool ready = s_chm_status.error == ESP_OK && s_chm_timers[0].handle && s_chm_timers[1].handle;
    portEXIT_CRITICAL_SAFE(&s_chm_lock);
    if (!ready) return 3;
    return __real_chm_start_op(channel, minimum_ms, maximum_ms, start, end, context);
}

void __wrap_chm_end_op_timeout_process(void *argument)
{
    uint32_t identity = (uint32_t)(uintptr_t)argument;
    uint64_t now = (uint64_t)esp_timer_get_time();
    int slot = -1;
    portENTER_CRITICAL_SAFE(&s_chm_lock);
    if (identity > 1 && s_chm_status.error == ESP_OK) {
        for (unsigned i = 0; i < 2; ++i)
            if (s_chm_timers[i].identity == identity && now >= s_chm_timers[i].deadline) slot = (int)i;
    }
    if (slot >= 0) { chm_invalidate_locked(0); chm_invalidate_locked(1); }
    else if (s_chm_status.ignored_messages != UINT32_MAX) ++s_chm_status.ignored_messages;
    portEXIT_CRITICAL_SAFE(&s_chm_lock);
    if (slot < 0) return;
    /* The native task serializes validation with end_op and its callback. Both
     * arms lose authority before that callback can start a successor. */
    if (g_chm && g_chm[4] != UINT8_MAX)
        __real_chm_end_op_timeout_process((void *)(uintptr_t)(unsigned)slot);
}

esp_err_t esp32qjs_wifi_chm_timer_service_native(void)
{
    uint64_t now = (uint64_t)esp_timer_get_time();
    uint32_t tickets[2] = {0};
    portENTER_CRITICAL_SAFE(&s_chm_lock);
    esp_err_t error = s_chm_status.error;
    for (unsigned i = 0; i < 2; ++i)
        if (s_chm_timers[i].identity && now >= s_chm_timers[i].deadline) tickets[i] = s_chm_timers[i].identity;
    portEXIT_CRITICAL_SAFE(&s_chm_lock);
    if (error != ESP_OK) return error;
    for (unsigned i = 0; i < 2; ++i)
        if (tickets[i]) __wrap_chm_end_op_timeout_process((void *)(uintptr_t)tickets[i]);
    return ESP_OK;
}

void esp32qjs_wifi_chm_timer_status(esp32qjs_wifi_chm_timer_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL_SAFE(&s_chm_lock);
    *out = s_chm_status;
    for (unsigned i = 0; i < 2; ++i) {
        out->timers_held += s_chm_timers[i].handle != NULL;
        out->active_arms += s_chm_timers[i].identity != 0;
        if (out->cleanup_error == ESP_OK) out->cleanup_error = s_chm_timers[i].cleanup_error;
    }
    portEXIT_CRITICAL_SAFE(&s_chm_lock);
}
#endif
