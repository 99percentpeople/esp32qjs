"""Registrar walk-time timers: pointer-free tickets and checked admission.

Called only after the parent patcher verifies the fixed SDK source hash. All
registry accesses use the SDK Wi-Fi task; this is not a general queue drain.
"""
from sdk_patches.common.source import function, replace

TIMERS = '''/* Live registrar list is intrusive in SDK-owned allocations. It does
 * not extend their lifetime. Queued callbacks contain only non-reused numbers,
 * never a registrar pointer; cancellation revokes the number before removal. */
static struct wps_registrar *esp32qjs_wps_registrars;
static uint64_t esp32qjs_wps_registrar_last_ticket;

static void esp32qjs_wps_registrar_timer_fire(void *high, void *low)
{
    uint64_t ticket = ((uint64_t)(uintptr_t)high << 32) |
                      (uint32_t)(uintptr_t)low;
    if (!ticket) return;
    for (struct wps_registrar *reg = esp32qjs_wps_registrars; reg;
         reg = reg->esp32qjs_next) {
        for (unsigned i = 0; i < 2; ++i) {
            if (reg->esp32qjs_timer_tickets[i] != ticket) continue;
            reg->esp32qjs_timer_tickets[i] = 0;
            if (i == 0) wps_registrar_pbc_timeout(reg, NULL);
            else wps_registrar_set_selected_timeout(reg, NULL);
            /* The callback may trigger shutdown. No access after dispatch. */
            return;
        }
    }
}

static int esp32qjs_wps_registrar_timer_index(eloop_timeout_handler handler)
{
    if (handler == wps_registrar_pbc_timeout) return 0;
    if (handler == wps_registrar_set_selected_timeout) return 1;
    return -1;
}

static int esp32qjs_wps_registrar_timer_cancel(eloop_timeout_handler handler,
    struct wps_registrar *reg, void *unused)
{
    int index = esp32qjs_wps_registrar_timer_index(handler);
    if (!reg || unused || index < 0) return ESP_ERR_INVALID_ARG;
    uint64_t ticket = reg->esp32qjs_timer_tickets[index];
    reg->esp32qjs_timer_tickets[index] = 0;
    if (!ticket) return 0;
    return eloop_cancel_timeout(esp32qjs_wps_registrar_timer_fire,
        (void *)(uintptr_t)(ticket >> 32), (void *)(uintptr_t)(uint32_t)ticket);
}

static int esp32qjs_wps_registrar_timer_arm(unsigned seconds, unsigned useconds,
    eloop_timeout_handler handler, struct wps_registrar *reg)
{
    int index = esp32qjs_wps_registrar_timer_index(handler);
    if (!reg || index < 0) return ESP_ERR_INVALID_ARG;
    struct wps_registrar *live = esp32qjs_wps_registrars;
    while (live && live != reg) live = live->esp32qjs_next;
    if (!live) return ESP_ERR_INVALID_STATE;
    if (esp32qjs_wps_registrar_last_ticket == UINT64_MAX) return ESP_ERR_NO_MEM;
    uint64_t ticket = ++esp32qjs_wps_registrar_last_ticket;
    /* Registration does not invoke the handler inline in the fixed SDK. On
     * failure preserve the old timer and selected/PBC state. Consumed tickets
     * never roll back, including across disable/reopen or runtime restart. */
    int ret = eloop_register_timeout(seconds, useconds,
        esp32qjs_wps_registrar_timer_fire, (void *)(uintptr_t)(ticket >> 32),
        (void *)(uintptr_t)(uint32_t)ticket);
    if (ret != 0) return ret;
    esp32qjs_wps_registrar_timer_cancel(handler, reg, NULL);
    reg->esp32qjs_timer_tickets[index] = ticket;
    return 0;
}

/* Close prefix only: revoke scheduled tickets without freeing the registrar
 * still referenced by existing peer EAP state. Fixed eloop cancellation returns
 * a count, not an SDK error. It is not evidence of other callbacks draining. */
int esp32qjs_wps_registrar_cancel_timers(struct wps_registrar *reg)
{
    if (!reg) return ESP_OK;
    esp32qjs_wps_registrar_timer_cancel(wps_registrar_pbc_timeout, reg, NULL);
    esp32qjs_wps_registrar_timer_cancel(wps_registrar_set_selected_timeout, reg, NULL);
    return ESP_OK;
}

static void esp32qjs_wps_registrar_timer_detach(struct wps_registrar *reg)
{
    esp32qjs_wps_registrar_timer_cancel(wps_registrar_pbc_timeout, reg, NULL);
    esp32qjs_wps_registrar_timer_cancel(wps_registrar_set_selected_timeout, reg, NULL);
    struct wps_registrar **slot = &esp32qjs_wps_registrars;
    while (*slot && *slot != reg) slot = &(*slot)->esp32qjs_next;
    if (*slot) *slot = reg->esp32qjs_next;
    reg->esp32qjs_next = NULL;
}
'''


def patch_registrar_timers(text: str) -> str:
    for header in ('wps_i.h', 'wps_dev_attr.h'):
        text = replace(text, f'#include "{header}"', f'#include "wps/{header}"')
    text = replace(text, '#include "utils/includes.h"',
                   '#include "utils/includes.h"\n#include "esp_err.h"\n#include <stdint.h>')
    text = replace(text, 'struct wps_registrar {\n', '''struct wps_registrar {
    struct wps_registrar *esp32qjs_next;
    uint64_t esp32qjs_timer_tickets[2];
''')
    anchor = '''static void wps_registrar_set_selected_timeout(void *eloop_ctx,
\t\t\t\t\t       void *timeout_ctx);'''
    text = replace(text, anchor, anchor + '\n\n' + TIMERS)
    start = text.index('struct wps_registrar *\nwps_registrar_init(')
    end = text.index('\n}\n', start) + 3
    body = text[start:end]
    fixed = replace(body, '\treturn reg;', '''    reg->esp32qjs_next = esp32qjs_wps_registrars;
    esp32qjs_wps_registrars = reg;
\treturn reg;''')
    text = replace(text, body, fixed)
    # Replace only the two reviewed timer handlers. Synchronous direct calls
    # continue using a live registrar; they do not manufacture timer tickets.
    for name in ('wps_registrar_pbc_timeout', 'wps_registrar_set_selected_timeout'):
        text = text.replace(f'eloop_cancel_timeout({name}, reg, NULL)',
                            f'esp32qjs_wps_registrar_timer_cancel({name}, reg, NULL)')
    body = function(text, 'wps_registrar_deinit')
    fixed = replace(body, '''\tesp32qjs_wps_registrar_timer_cancel(wps_registrar_pbc_timeout, reg, NULL);
\tesp32qjs_wps_registrar_timer_cancel(wps_registrar_set_selected_timeout, reg, NULL);''',
                    '    esp32qjs_wps_registrar_timer_detach(reg);')
    text = replace(text, body, fixed)

    pin_timer = '''\teloop_register_timeout(WPS_PBC_WALK_TIME, 0,
\t\t\t       wps_registrar_set_selected_timeout,
\t\t\t       reg, NULL);'''
    body = function(text, 'wps_registrar_add_pin')
    fixed = replace(body, '\tif (p->wildcard_uuid)\n', '''    int timer_error = esp32qjs_wps_registrar_timer_arm(WPS_PBC_WALK_TIME, 0,
        wps_registrar_set_selected_timeout, reg);
    if (timer_error != 0) {
        wps_free_pin(p);
        return timer_error;
    }
\tif (p->wildcard_uuid)
''')
    fixed = replace(fixed, '\tesp32qjs_wps_registrar_timer_cancel(wps_registrar_set_selected_timeout, reg, NULL);\n' + pin_timer, '')
    text = replace(text, body, fixed)

    body = function(text, 'wps_registrar_button_pushed')
    fixed = replace(body, '\treg->force_pbc_overlap = 0;', '''    int timer_error = esp32qjs_wps_registrar_timer_arm(WPS_PBC_WALK_TIME, 0,
        wps_registrar_pbc_timeout, reg);
    if (timer_error != 0) return timer_error;
    esp32qjs_wps_registrar_timer_cancel(wps_registrar_set_selected_timeout, reg, NULL);
\treg->force_pbc_overlap = 0;''')
    fixed = replace(fixed, '''\tesp32qjs_wps_registrar_timer_cancel(wps_registrar_set_selected_timeout, reg, NULL);
\tesp32qjs_wps_registrar_timer_cancel(wps_registrar_pbc_timeout, reg, NULL);
\teloop_register_timeout(WPS_PBC_WALK_TIME, 0, wps_registrar_pbc_timeout,
\t\t\t       reg, NULL);''', '')
    text = replace(text, body, fixed)

    # NFC is not enabled in the current target contexts. Keep the compiled-out
    # SDK branch on the same timer mechanism rather than retaining raw pointers.
    body = function(text, 'wps_registrar_add_nfc_pw_token')
    fixed = replace(body, '\tdl_list_add(&reg->nfc_pw_tokens, &token->list);', '''    int timer_error = esp32qjs_wps_registrar_timer_arm(WPS_PBC_WALK_TIME, 0,
        wps_registrar_set_selected_timeout, reg);
    if (timer_error != 0) {
        bin_clear_free(token, sizeof(*token));
        return timer_error;
    }
\tdl_list_add(&reg->nfc_pw_tokens, &token->list);''')
    fixed = replace(fixed, '\tesp32qjs_wps_registrar_timer_cancel(wps_registrar_set_selected_timeout, reg, NULL);\n' + pin_timer, '')
    text = replace(text, body, fixed)
    return text
