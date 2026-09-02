# Pin and resource rules

Inspect all registered immutable hardware constants with `sys.config()` before
choosing pins. Use `sys.config(key)` only when the exact key is already known.
Hardware identity lives under the lazy `sys.info.hardware` tree; board wiring
does not.
`gpio.USER_LED_PIN` and `gpio.LED_BUILTIN` may be `-1`. Do not infer a safe pin
from the MCU family alone.

Hardware defaults use this precedence:

1. an explicit API option;
2. a hardware-profile constant embedded when the firmware was built;
3. a framework Kconfig default that is safe for the selected MCU;
4. an error when none exists.

`sys.config()` returns a fresh plain object containing all registered values;
this is the authoritative pin/default inventory for the running firmware.
`sys.config(key)` returns one string, number, boolean, or `undefined`.
Registered framework keys use the `ESP32QJS_*` prefix. Deployment-specific
custom values should use an application prefix such as `APP_*`; do not invent
new `ESP32QJS_*` names. Constants are read-only at runtime.

```js
(function () {
    return sys.config();
})()
```

Native hardware handles consume bounded resources. Close I2C, SPI, UART, RMT,
I2S, queue, and display handles in `finally`. ADC units and DAC channels are
addressed by numeric IDs and must also be closed explicitly. Stop/deconfigure
PWM channels and timers created by temporary code.
