"""Repair the pinned SDK antenna GPIO write boundary in a build copy.

The caller still owns cross-modem admission and GPIO routing/rollback. This
patch validates the whole input before mutation, passes a mask to the reserve
API, and preserves gpio_config errors rather than connecting a failed pin.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

SOURCE_HASH = '605d99d515c536ca096e8db54ff10cfa177db635073e2101ecba14e828da57e8'
INIT_HASH = '8743914fc5303075f04a926f4fe64465c9fdb4d903800594d620e1536bde901d'

IDLE_TRANSACTION = r'''
/* SDK access lock serializes this callback with every PHY enable/disable.
 * The callback is native, synchronous and must not call PHY enable/disable.
 * Modem sleep is not controller retirement; the framework also checks logical
 * owners, including a BLE open/close operation, inside this native window. */
esp_err_t esp32qjs_phy_antenna_run_idle(esp_err_t (*apply)(void *), void *context)
{
    if (apply == NULL) return ESP_ERR_INVALID_ARG;
    _lock_acquire(&s_phy_access_lock);
    esp_err_t error = phy_get_modem_flag() == 0 ? apply(context) : ESP_ERR_INVALID_STATE;
    _lock_release(&s_phy_access_lock);
    return error;
}
'''

GPIO_WRITE = r'''static esp_err_t phy_ant_set_gpio_output(uint32_t io_num, bool owned)
{
    gpio_config_t io_conf = {0};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    /* The framework already reserved its pins atomically. Configuring them
     * disabled avoids gpio_config claiming the same pin and reporting a false
     * conflict; enable output only after the rest of configuration succeeded. */
    io_conf.mode = owned ? GPIO_MODE_DISABLE : GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (UINT64_C(1) << io_num);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    esp_err_t error = gpio_config(&io_conf);
    return error == ESP_OK && owned ? gpio_output_enable(io_num) : error;
}

static esp_err_t esp32qjs_phy_set_ant_gpio(esp_phy_ant_gpio_config_t *config, uint64_t owned)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    uint64_t pins = 0;
    for (int i = 0; i < 4; i++) {
        if (!config->gpio_cfg[i].gpio_select) continue;
        int pin = config->gpio_cfg[i].gpio_num;
        if (pin >= 64 || !GPIO_IS_VALID_OUTPUT_GPIO(pin)) return ESP_ERR_INVALID_ARG;
        uint64_t mask = UINT64_C(1) << pin;
        if ((pins & mask) || (!(owned & mask) && esp_gpio_is_reserved(mask))) return ESP_ERR_INVALID_ARG;
        pins |= mask;
    }
    for (int i = 0; i < 4; i++) {
        if (!config->gpio_cfg[i].gpio_select) continue;
        esp_err_t error = phy_ant_set_gpio_output(config->gpio_cfg[i].gpio_num,
            (owned & (UINT64_C(1) << config->gpio_cfg[i].gpio_num)) != 0);
        if (error != ESP_OK) return error;
        esp_rom_gpio_connect_out_signal(config->gpio_cfg[i].gpio_num, s_phy_ant_sel_sig_idx[i], 0, 0);
    }
    /* On a partial write the caller must retain its transaction and restore
     * touched routes. This saved config is committed only after full success. */
    memcpy(&s_phy_ant_gpio_config, config, sizeof(s_phy_ant_gpio_config));
    return ESP_OK;
}

esp_err_t esp_phy_set_ant_gpio(esp_phy_ant_gpio_config_t *config)
{
    return esp32qjs_phy_set_ant_gpio(config, 0);
}

/* Framework-only entry: the caller atomically reserved every selected pin,
 * keeps the native PHY access lock, and owns route rollback on every failure. */
esp_err_t esp32qjs_phy_set_ant_gpio_owned(esp_phy_ant_gpio_config_t *config, uint64_t owned)
{
    return esp32qjs_phy_set_ant_gpio(config, owned);
}

void esp32qjs_phy_ant_gpio_restore_config(const esp_phy_ant_gpio_config_t *config)
{
    memcpy(&s_phy_ant_gpio_config, config, sizeof(s_phy_ant_gpio_config));
}

unsigned esp32qjs_phy_ant_gpio_signal(unsigned index)
{
    return index < 4 ? s_phy_ant_sel_sig_idx[index] : UINT32_MAX;
}

'''


def patch_source(source: bytes) -> str:
    if hashlib.sha256(source).hexdigest() != SOURCE_HASH:
        raise ValueError('Unreviewed esp_phy antenna GPIO source')
    text = source.decode('utf-8')
    start = text.index('static void phy_ant_set_gpio_output(')
    end = text.index('esp_err_t esp_phy_get_ant_gpio(', start)
    return text[:start] + GPIO_WRITE + text[end:]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--phy-init', type=Path)
    parser.add_argument('--init-output', type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error('A separate build output is required; shared SDK input is read-only')
    prepared = patch_source(args.source.read_bytes())
    if bool(args.phy_init) != bool(args.init_output):
        parser.error('--phy-init and --init-output are required together')
    if args.phy_init:
        original = args.phy_init.read_bytes()
        if args.phy_init.resolve() == args.init_output.resolve() or hashlib.sha256(original).hexdigest() != INIT_HASH:
            parser.error('Unreviewed PHY access lock source or shared output')
        args.init_output.parent.mkdir(parents=True, exist_ok=True)
        args.init_output.write_text(original.decode('utf-8') + IDLE_TRANSACTION, encoding='utf-8')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(prepared, encoding='utf-8')
    print('Prepared SDK antenna GPIO validation and error propagation')


if __name__ == '__main__':
    main()
