# Hardware API and safe examples

## Pin and resource rules

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

## GPIO

- constants: `gpio.DISABLED`, `INPUT`, `OUTPUT`, `INPUT_OUTPUT`, `OUTPUT_OPEN_DRAIN`, `INPUT_OUTPUT_OPEN_DRAIN`
- pulls: `gpio.FLOATING`, `PULLUP`, `PULLDOWN`, `PULLUP_PULLDOWN`
- edges: `gpio.CHANGE`, `RISING`, `FALLING`, `LOW`, `HIGH`
- `gpio.isValid(pin)` / `gpio.isOutputCapable(pin)`
- `gpio.configure(pin, options)` / `pinMode(pin, mode)` / `setPull(pin, pull)`
- `gpio.status(pin)` / `digitalRead(pin)` / `digitalWrite(pin, value)` / `toggle(pin)`
- `gpio.getDriveStrength(pin)` / `setDriveStrength(pin, strength)` / `hold(pin, enabled)`
- `gpio.watch(pin, mode?)` returns a bounded `EventQueue` of
  `{pin, level, mode}` records; the ISR never invokes JavaScript
- close the returned queue to detach the interrupt
- `gpio.reset(pin)`
- `gpio.led(on)` drives the configured user LED with active-low handling.

Use `gpio.led` when possible:

```js
(function () {
    if (gpio.USER_LED_PIN < 0 && gpio.LED_BUILTIN < 0) {
        return { changed: false, reason: "No configured LED pin" };
    }
    gpio.led(true);
    return {
        changed: true,
        pin: gpio.USER_LED_PIN >= 0 ? gpio.USER_LED_PIN : gpio.LED_BUILTIN,
        activeLow: gpio.USER_LED_ACTIVE_LOW
    };
})()
```

Do not manually invert `gpio.led`; it already applies `gpio.USER_LED_ACTIVE_LOW`.

Do not run a top-level loop around `queue.receive(timeout)`: even though a
direct receive waits cooperatively, the current startup call stack remains
occupied. Arm one native receive Future at a time and rearm after it settles:

```js
var events = gpio.watch(9, gpio.FALLING);
var watching = true;

function arm() {
    if (!watching) return;
    Future.call(events.receive, events, []).map(function (event) {
        if (!watching || event === null) return;
        print(event.pin, event.level);
        arm();
    });
}

arm();
```

Set `watching = false` and call `events.close()` during teardown.

## LEDC PWM

- `ledc.timerConfig(timer, {freqHz, dutyResolution, clock?})`
- `ledc.channelConfig(channel, {pin, timer, duty?, hpoint?, outputInvert?, sleepMode?})`
- `ledc.timerStatus(timer)` / `channelStatus(channel)`
- `ledc.setDuty(channel, duty)` / `setDutyWithHpoint(channel, duty, hpoint)`
- `ledc.setDutyAndUpdate(channel, duty, hpoint?)` / `updateDuty(channel)`
- `ledc.getDuty(channel)` / `getHpoint(channel)`
- `ledc.setFreq(timer, freqHz)` / `getFreq(timer)` / `bindChannelTimer(channel, timer)`
- `ledc.stop(channel, idleLevel?)`
- `ledc.timerPause(timer)` / `timerResume(timer)`
- deconfigure with `ledc.channelConfig(channel, {deconfigure: true})` and `ledc.timerConfig(timer, {deconfigure: true})`.

Correct temporary PWM:

```js
(function () {
    var pin = gpio.USER_LED_PIN >= 0 ? gpio.USER_LED_PIN : gpio.LED_BUILTIN;
    if (pin < 0) {
        return { ok: false, reason: "No configured LED pin" };
    }
    ledc.timerConfig(0, { freqHz: 5000, dutyResolution: 8 });
    ledc.channelConfig(0, {
        pin: pin,
        timer: 0,
        duty: 0,
        outputInvert: gpio.USER_LED_ACTIVE_LOW
    });
    ledc.setDutyAndUpdate(0, 128, 0);
    return { ok: true, timer: ledc.timerStatus(0), channel: ledc.channelStatus(0) };
})()
```

For cleanup, call `ledc.stop(0, gpio.USER_LED_ACTIVE_LOW)` so the configured
LED is physically off, deconfigure channel 0, then timer 0. Do not replay setup
after an uncertain mutation without inspecting both statuses.

## ADC and DAC

- ADC constants include `UNIT_1`, `UNIT_2`, `ATTEN_DB_0`, `ATTEN_DB_2_5`,
  `ATTEN_DB_6`, `ATTEN_DB_12`, and supported `BITWIDTH_*` values.
- `adc.ioToChannel(pin)` returns `{unit, channel}` or `null`.
- `adc.channelToIo(unit, channel)` returns a GPIO or `null`.
- `adc.open(unit)` / `adc.close(unit)` / `adc.status(unit)`
- `adc.configure(unit, channel, {atten?, bitwidth?})`
- `adc.read(unit, channel)` / `adc.readMilliVolts(unit, channel)`
- DAC constants include `CHANNEL_0`, `CHANNEL_1`, `CHANNEL_COUNT`,
  `RESOLUTION_BITS`, and `MAX_VALUE`.
- `dac.ioToChannel(pin)` returns `{channel, pin}` or `null`.
- `dac.channelToIo(channel)` returns a GPIO.
- `dac.open(channel)` / `dac.close(channel)` / `dac.status(channel?)`
- `dac.write(channel, value)`

ADC availability, attenuation, and bit width vary by MCU. DAC is absent on many chips; check `sys.info.features.dac`.

```js
(function (sensorPin) {
    var ref = adc.ioToChannel(sensorPin);
    if (!ref) return { supported: false };
    adc.open(ref.unit);
    try {
        adc.configure(ref.unit, ref.channel, {
            atten: adc.ATTEN_DB_12,
            bitwidth: adc.BITWIDTH_12
        });
        return { raw: adc.read(ref.unit, ref.channel) };
    } finally {
        adc.close(ref.unit);
    }
})(0)
```

The final `0` is an example only; replace it with the user-provided sensor pin
after checking the wiring.

## I2C

`i2c.openBus({sda?, scl?, freqHz?, timeoutMs?, internalPullup?})` returns a bus.

- `scan()`
- `read(address, length)`
- `write(address, data)`
- `writeChunks(address, chunks)`
- `writeSegments(address, segments)` writes byte-source segments in one
  transaction without joining them into a JavaScript byte array.
- `writeRead(address, data, length)`
- `status()`
- `close()`

Safe scan:

```js
(function () {
    var bus = i2c.openBus({ freqHz: 100000, timeoutMs: 50 });
    try {
        return { addresses: bus.scan() };
    } finally {
        bus.close();
    }
})()
```

Omitted values first use `ESP32QJS_I2C_SDA`, `ESP32QJS_I2C_SCL`,
`ESP32QJS_I2C_FREQ_HZ`, `ESP32QJS_I2C_TIMEOUT_MS`, and
`ESP32QJS_I2C_INTERNAL_PULLUP` when configured. Otherwise use explicit
user-provided pins; never guess them.

## SPI

- `spi.openBus({host?, sclk?, mosi?, miso?, maxTransferSize?, dmaStagingBytes?})`
- bus `openDevice({cs?, mode?, freqHz?, queueSize?, csHigh?, lsbFirst?, directExternalDma?, timeoutMs?})`
- device `transfer(data, {timeoutMs?}?)`, `read(length, {fillByte?, timeoutMs?}?)`, `write(data, {timeoutMs?}?)`,
  `writeChunks(chunks, {queueDepth?, timeoutMs?}?)`, `writeSource(source, {queueDepth?, timeoutMs?}?)`, `status()`,
  `close()`
- bus `status()`, `close()`

Close the device before the bus. Do not use flash/PSRAM pins or guessed chip-select pins.
Omitted bus/device options may use the registered `ESP32QJS_SPI_*` constants.

## UART

`uart.open({port?, tx?, rx?, baud?, dataBits?, parity?, stopBits?, rxBufferSize?, txBufferSize?, timeoutMs?})` returns a port.

- `available()`
- `read(length, timeoutMs?)`
- `write(data)`
- `writeChunks(chunks)` / `writeSource(source)`
- `flush(timeoutMs?)`
- `clearRx()`
- `status()`
- `close()`

The Agent transport may own USB Serial/JTAG; never open, send, or close `usbSerial` from model-issued code.
Omitted UART options may use the registered `ESP32QJS_UART_*` constants. An
explicit option always wins over the profile value.

## RMT pulse symbols

Check `sys.info.features.rmt`, then use `rmt.capabilities()` to inspect the
target's DMA support and memory-symbol limits. Never infer a safe pin.

- `rmt.createSymbols(capacity)` returns a native `RMTSymbolBuffer` with
  `capacity`, `length`, `push(...)`, `get(index)`, `set(index, ...)`, `clear()`,
  and idempotent `close()`.
- Each symbol contains `duration0Ticks`, `level0`, `duration1Ticks`, and
  `level1`. Durations are limited to 32767 ticks.
- `rmt.open({ direction, pin, resolutionHz, memorySymbols?, dma?, invert? })`
  returns one RX or TX channel. `resolutionHz` is required.
- Call `start()` before `transmit(...)` or `receive(...)`; call `stop()` after
  bounded work and `close()` in `finally`.
- TX is `transmit(symbols, { loopCount?, endLevel?, timeoutMs? })`. Looping must
  be finite.
- RX is `receive(symbols, { minPulseNs?, idleThresholdNs, timeoutMs? })` and
  fills the caller's logical buffer. It returns `{ length, truncated }` or
  `null` at timeout.
- Only one operation may be pending per channel. Use `Future.call()` when the
  current runtime position must remain available. Cancellation and close apply
  to the complete hardware operation.

```js
(function (pin) {
    if (typeof pin !== "number") {
        return { ok: false, reason: "APP_RMT_TX_PIN is not configured" };
    }
    var symbols = rmt.createSymbols(2);
    var channel = null;
    try {
        symbols.push(560, 1, 560, 0);
        symbols.push(560, 1, 1690, 0);
        channel = rmt.open({
            direction: "tx",
            pin: pin,
            resolutionHz: 1000000
        });
        channel.start();
        return channel.transmit(symbols, {
            loopCount: 0,
            timeoutMs: 100
        });
    } finally {
        if (channel !== null) channel.close();
        symbols.close();
    }
})(sys.config("APP_RMT_TX_PIN"))
```

The pin key above is only an application example. If the selected hardware
profile does not define it, stop and ask for the wiring. Decode NEC or any
other waveform in workspace JavaScript; do not assume protocol semantics in
the native buffer.
