# API Stability Plan

This document is a working proposal for a long-lived JavaScript host API.
It is intentionally more opinionated than [docs/c-api.md](c-api.md): the goal here is not to describe today's implementation, but to define which API shapes should be frozen, which should still change before freeze, and how new modules such as `dac` should fit in.

This plan covers:

- built-in C-side host APIs exported by the firmware runtime
- board/peripheral bindings such as `gpio`, `ledc`, `adc`, `dac`, `i2c`, `spi`, and `uart`
- low-level native helpers such as `displayBuffer`
- transport/runtime helpers such as `wifi`, `http`, timers, and `load(...)`

It does not treat JS-side LittleFS libraries such as `display` and `ui` as firmware ABI. Those should remain versioned JS libraries layered on top of the built-in host APIs.

> Earlier callback and `*.async` recommendations are obsolete. The implemented
> synchronous I/O, `Future`, and `EventQueue` contracts are documented in
> [C API Reference](c-api.md). The remaining module-shape and feature-gating
> guidance here still applies.

## Goals

The stable API should satisfy these rules:

- Module names stay small and literal. Prefer raw ESP-IDF or platform names such as `gpio`, `ledc`, `adc`, `dac`, `i2c`, `spi`, `uart`, `wifi`, `http`, `socket`, `sys`, `fs`, `nvs`.
- Built-in host APIs stay low-level. Board-independent drivers, widgets, protocol stacks, debounce logic, animation helpers, and other policy belong in JavaScript.
- Additive change is preferred. Once a module shape is frozen, new fields and methods may be added, but existing names and semantics should not be renamed or weakened.
- Host modules should be board-selectable features. Each optional module should be enabled or disabled by a `CONFIG_...` feature macro and chosen per board profile under `configs/boards/<board>/sdkconfig.defaults`.
- Compiled feature sets should be discoverable from JS in one stable place such as `sys.info().features`, instead of forcing user scripts to probe globals with `typeof`.
- Mutating peripheral calls should return state objects when that improves observability, but status objects must reflect real or intentionally tracked state, not guessed state.
- ISR and background-task events must always be bridged back onto the single JS runtime task.
- I/O modules expose one synchronous method shape without callback overloads or `.async` namespaces. Concurrent native I/O is started only through the global `Future` facility.

## Stability Levels

This document uses four levels:

- `Stable now`
  The current API shape is already close to what should be frozen.
- `Candidate for freeze`
  The module has already been adjusted to the intended shape, but should still receive focused tests and reference-doc cleanup before being declared stable.
- `Adjust before freeze`
  The module is useful, but one or more design choices should be corrected before calling it stable.
- `Planned`
  The module is not fully implemented yet, but its public shape should be decided now.

## Feature Gating Model

The built-in host API should be split into:

- `Core runtime surface`
  Small helpers and types that are effectively part of the runtime itself.
- `Optional host features`
  Peripheral or connectivity modules that may be compiled in or out per board.

Recommended compile-time model:

- Define one `Kconfig` boolean for each optional module under [components/esp32_mquickjs/Kconfig.projbuild](../components/esp32_mquickjs/Kconfig.projbuild).
- Use the generated `CONFIG_...` macros in C to:
  - compile out module registration from [`src/core/mqjs_stdlib_esp32.c`](../components/esp32_mquickjs/src/core/mqjs_stdlib_esp32.c)
  - compile out implementation files or guard their registration paths
  - conditionally include component dependencies
- Use those generated `CONFIG_...` symbols directly in source code; do not add a second alias layer such as `ESP32_MQUICKJS_FEATURE_*`.
- Let each board profile set defaults in `configs/boards/<board>/sdkconfig.defaults`.

Recommended feature symbols:

- `CONFIG_ESP32_MQUICKJS_FEATURE_FS`
- `CONFIG_ESP32_MQUICKJS_FEATURE_NVS`
- `CONFIG_ESP32_MQUICKJS_FEATURE_GPIO`
- `CONFIG_ESP32_MQUICKJS_FEATURE_LEDC`
- `CONFIG_ESP32_MQUICKJS_FEATURE_ADC`
- `CONFIG_ESP32_MQUICKJS_FEATURE_DAC`
- `CONFIG_ESP32_MQUICKJS_FEATURE_I2C`
- `CONFIG_ESP32_MQUICKJS_FEATURE_SPI`
- `CONFIG_ESP32_MQUICKJS_FEATURE_UART`
- `CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL`
- `CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET`
- `CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET`
- `CONFIG_ESP32_MQUICKJS_FEATURE_WIFI`
- `CONFIG_ESP32_MQUICKJS_FEATURE_HTTP`
- `CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER`
- `CONFIG_ESP32_MQUICKJS_FEATURE_DISPLAY_BUFFER`

Recommended dependency rules:

- `FEATURE_NVS` has no peripheral dependency and conditionally links `nvs_flash`
- `FEATURE_ADC` depends on `SOC_ADC_SUPPORTED`
- `FEATURE_DAC` depends on `SOC_DAC_SUPPORTED`
- `FEATURE_I2C` depends on `SOC_I2C_SUPPORTED`
- `FEATURE_LEDC` depends on `SOC_LEDC_SUPPORTED`
- `FEATURE_SPI` depends on `SOC_GPSPI_SUPPORTED`
- `FEATURE_UART` depends on `SOC_UART_SUPPORTED`
- `FEATURE_USB_SERIAL` depends on `SOC_USB_SERIAL_JTAG_SUPPORTED` and conflicts with the REPL frontend
- `FEATURE_SOCKET` has no application-protocol dependency and can use any initialized network interface
- `FEATURE_WEBSOCKET` depends on `FEATURE_WIFI`
- `FEATURE_WIFI` depends on `SOC_WIFI_SUPPORTED`
- `FEATURE_HTTP` depends on `FEATURE_WIFI` in the current firmware, unless another network backend is introduced later
- `FEATURE_HTTP_SERVER` depends on `FEATURE_WIFI` in the current firmware
- `FEATURE_DISPLAY_BUFFER` has no direct peripheral dependency, but full-frame RGB buffers should be enabled per board with memory budget in mind
- `staticFileHandler` is a composite capability that depends on `FEATURE_HTTP_SERVER && FEATURE_FS`
- `FEATURE_FS` stays enabled on most boards because `load(...)`, LittleFS startup, and static file serving depend on it

Recommended runtime discovery:

- Add a stable `features` object to `sys.info()`:

```js
print(JSON.stringify(sys.info().features));
// Example:
// {
//   fs: true,
//   nvs: false,
//   gpio: true,
//   ledc: true,
//   adc: true,
//   dac: false,
//   i2c: true,
//   spi: true,
//   uart: true,
//   usbSerial: false,
//   websocket: false,
//   displayBuffer: true,
//   wifi: true,
//   http: true,
//   httpServer: true,
//   staticFileHandler: true
// }
```

Behavior rule:

- If a feature is disabled at compile time, that module is not registered into the JS global object.
- Cross-board scripts should prefer `sys.info().features.<name>` over probing module globals directly.

## Board Profiles as Feature Presets

Board directories under [configs/boards](../configs/boards) should become the canonical place where runtime feature sets are selected.

Each board's `sdkconfig.defaults` should describe:

- chip-independent board identity
- pin defaults
- memory defaults
- enabled host features for that board

Recommended examples for current boards:

`esp32c3_supermini`

- `FEATURE_FS=y`
- `FEATURE_GPIO=y`
- `FEATURE_LEDC=y`
- `FEATURE_ADC=y`
- `FEATURE_DAC=n`
- `FEATURE_I2C=y`
- `FEATURE_SPI=y`
- `FEATURE_DISPLAY_BUFFER=y`
- `FEATURE_WIFI=y`
- `FEATURE_HTTP=y`
- `FEATURE_HTTP_SERVER=y`

`xiao_esp32s3`

- `FEATURE_FS=y`
- `FEATURE_GPIO=y`
- `FEATURE_LEDC=y`
- `FEATURE_ADC=y`
- `FEATURE_DAC=n`
- `FEATURE_I2C=y`
- `FEATURE_SPI=y`
- `FEATURE_DISPLAY_BUFFER=y`
- `FEATURE_WIFI=y`
- `FEATURE_HTTP=y`
- `FEATURE_HTTP_SERVER=y`

Future `esp32` or `esp32s2` boards can enable `FEATURE_DAC=y` when DAC pins are actually usable on the board.

Board-level enablement should reflect both:

- chip capability
- practical board usefulness

For example, a chip may support DAC while a specific board routes those pins poorly or reserves them for another function; in that case the board profile should still be free to disable the feature.

## Module Inventory

Built-in modules and types currently in scope:

- Global helpers: `help`, `load`, `defer`, `waitFor`, `sleep`, `delay`, `setTimeout`, `clearTimeout`, `setInterval`, `clearInterval`, `gc`
- Data/runtime types: `Headers`, `Request`, `Response`, `Stream`
- Filesystem/runtime modules: `fs`, `nvs`, `sys`
- Peripheral modules: `gpio`, `ledc`, `adc`, `dac`, `i2c`, `spi`, `uart`
- Low-level graphics buffer modules: `displayBuffer`
- Transport/connectivity modules: `usbSerial`, `socket`, `websocketClient`, `wifi`, `http`, `HttpServer`, `StaticFileHandler`
- Generic concurrency types: planned `Future` and `EventQueue`
- JS-side libraries outside the firmware ABI: `display`, `ui`

In the long-term plan, `nvs`, `gpio`, `ledc`, `adc`, `dac`, `i2c`, `spi`, `uart`, `usbSerial`, `socket`, `websocketClient`, `displayBuffer`, `wifi`, `http`, and `httpServer` should all be treated as optional host features rather than unconditional globals.

## Freeze Principles By Area

### Global Helpers

Status: `Adjust before freeze`

The current inventory is:

- `load(path)`
- `defer()`
- `waitFor(start, timeoutMs?)`
- `sleep(ms)` / `delay(ms)`
- `setTimeout`, `clearTimeout`
- `setInterval`, `clearInterval`
- `gc()`
- `help()`

Freeze recommendations:

- Keep both `sleep` and `delay`; `delay` is a harmless compatibility alias.
- Replace `defer()` and `waitFor(...)` with the accepted scheduler-driven,
  deferred-start `Future` API.
- Do not require Promises, `.then()`, `.await`, or resumable JavaScript for the
  baseline firmware API.
- Keep `Future.call(...)` as the only custom asynchronous callback entry point;
  it queues any callable with one public lifecycle and lets the runtime dispatch
  it at the next JS idle point without requiring `wait()`. Native-driver lookup
  is an internal optimization, not a separate API mode.
- Keep nested waits lightweight: reuse the scheduler pump and normal JS call
  stack rather than adding fibers, dependency graphs, cycle detection, or
  automatic Future flattening.
- Keep callback-based timers as explicitly asynchronous generic tools.
- Keep `load(...)` as the single primitive for JS-side library composition.

### `fs` and `Stream`

Status: `Stable now`

Why:

- The LittleFS root boundary is explicit and easy to reason about.
- The API is small and composable.
- `Stream` is already shared consistently by `fs`, `Request`, and `Response`.

Freeze recommendations:

- Keep current path sandboxing under `/littlefs`.
- Keep current text-oriented convenience helpers such as `readText` and `writeText`.
- Keep `Stream` as the common file/request/response body abstraction.
- Treat `fs` as a feature-gated module in build configuration, but leave it enabled by default on normal boards.

Adjustments that can still be additive later:

- Add byte-oriented helpers later if binary workloads become common.
- Do not change existing string-returning APIs to return arrays or typed arrays.

### `nvs`

Status: `Candidate for freeze`

The optional module exposes only bounded strings in the default NVS partition:
`getString`, `setString`, `erase`, `clear`, `status`, and a 2048-byte limit.
Namespace/key syntax is intentionally restricted, updates and erases use purge
mode, and no partition erase, iteration, blob, or encryption-key API is
available.

Freeze recommendations:

- Keep NVS encryption provisioning outside this convenience binding; report the
  compiled encryption state explicitly through `status()`.
- Keep values bounded and keep namespace/key access explicit.
- Keep the module absent unless `FEATURE_NVS` is enabled.
- Treat this as trusted application persistence, not a per-namespace security
  sandbox.
- Add power-loss and encrypted-partition coverage before declaring it stable.

### `Headers`, `Request`, and `Response`

Status: `Stable now`

Why:

- They already form a coherent transport layer for `fetch(...)` and `http.server(...)`.
- The shapes are web-inspired without pretending to be the full browser Fetch API.

Freeze recommendations:

- Keep these types intentionally small.
- Avoid adding browser-only semantics that the firmware cannot honor consistently.
- Prefer small additive helpers over deep Fetch-compat work.

### `sys`

Status: `Stable now`

Why:

- `sys.info()`, `millis()`, `micros()`, `freeHeap()`, and bounded `randomHex()` are generic runtime/platform helpers.
- `sys.withTimeout()` provides a scoped wall-clock budget without extending an outer native deadline.
- The module is not overloaded with peripheral control.

Freeze recommendations:

- Keep this module focused on platform/runtime introspection.
- Do not move unrelated peripheral helpers into `sys`.
- If later adding power-management or reboot helpers, do so carefully and explicitly.
- Extend `sys.info()` with a stable `features` object so scripts can discover compiled host modules safely.

## Peripheral Modules

### `gpio`

Status: `Adjust before freeze`

Current shape is already close to the right long-term layer:

- raw pin configuration
- digital read/write
- pull, drive strength, hold
- interrupt delivery, currently registered through `attachInterrupt(...)`

Why it is in a good place:

- It stays close to ESP-IDF pad control instead of embedding drivers.
- Interrupt events are already safely bridged back onto the JS runtime task.
- Status inspection is strong enough for debugging.

Freeze recommendations:

- Keep `pinMode`, `setPull`, `digitalRead`, `digitalWrite`, `toggle`, `hold`, `reset`, `status`.
- Replace `attachInterrupt(pin, callback, mode)` / `detachInterrupt(pin)` with
  `watch(pin, mode): EventQueue` and queue `close()`.
- Keep interrupt event objects `{ pin, level, mode }` as queue values.
- Keep richer state inspection in `status(pin)`.
- Gate the whole module behind `FEATURE_GPIO`, even if most boards will leave it enabled.

Intentional non-goals:

- no built-in debounce
- no long-press / double-click helpers
- no Arduino-style `digitalPinToInterrupt(...)`
- no device-specific drivers

### `ledc`

Status: `Adjust before freeze`

The module is correctly placed as a low-level PWM timer/channel binding, but two parts should be tightened before it is considered frozen.

What is already good:

- timer and channel are modeled separately
- API maps closely to ESP-IDF LEDC primitives
- no servo, no `analogWrite(...)`, no animation policy in C

What should change before freeze:

- Status semantics should be stricter. Methods such as `setFreq(...)`, `bindChannelTimer(...)`, `timerPause(...)`, and `timerResume(...)` should not make a timer or channel appear fully configured unless it actually was configured.
- The module currently assumes `LEDC_LOW_SPEED_MODE`. That is acceptable for current targets, but the API plan should explicitly define whether speed mode is intentionally fixed or whether a later explicit capability field is expected.

Recommended stable shape:

- Keep current names:
  - `timerConfig`, `channelConfig`
  - `timerStatus`, `channelStatus`
  - `setDuty`, `setDutyWithHpoint`, `setDutyAndUpdate`, `updateDuty`
  - `getDuty`, `getHpoint`
  - `setFreq`, `getFreq`
  - `bindChannelTimer`
  - `stop`, `timerPause`, `timerResume`
- Keep raw timer/channel indices and raw duty integers.
- Do not add `analogWrite(...)` into this module.
- Gate the module behind `FEATURE_LEDC` so boards without PWM use-cases can drop it.

Future rule:

- High-level PWM abstractions belong in JS on top of `ledc`, not inside the native binding.

### `adc`

Status: `Stable now`

Why:

- The API cleanly exposes ESP-IDF oneshot ADC.
- Unit/channel mapping helpers are useful and low-level.
- Calibration support is surfaced without hiding hardware limits.

Freeze recommendations:

- Keep the explicit lifecycle:
  - `open(unit)`
  - `configure(unit, channel, options)`
  - `read(unit, channel)`
  - `readMilliVolts(unit, channel)`
  - `close(unit)`
- Keep `ioToChannel(pin)` and `channelToIo(unit, channel)`.
- Keep `status(unit)` returning full per-channel state.
- Gate the module behind `FEATURE_ADC`.

Intentional non-goals:

- no continuous mode
- no averaging
- no oversampling
- no sensor drivers

### `i2c`

Status: `Candidate for freeze`

The bus-object model is now in place and is close to the intended long-term shape.

What is already good:

- Small and understandable surface.
- Raw address + read/write operations are enough for JS-side device drivers.
- `scan()` is practical for REPL debugging.
- Generic `writeChunks(...)` accepts byte-source chunks without tying I2C to a display-specific object model.

What should stay intentional:

- The runtime now uses a bus-object model instead of a single global singleton, and future transport modules should follow that direction.
- The current `add device -> transact -> remove device` flow on every call is still acceptable for the first stable raw I2C surface, but it should remain an internal detail rather than leaking into the JS API shape.
- Display drivers may pass native byte chunks through this module, but I2C should not inspect `displayBuffer` internals.

Recommended stable target:

- Keep the bus-object model as the stable baseline:

```js
var bus = i2c.open({
  sda: i2c.DEFAULT_SDA,
  scl: i2c.DEFAULT_SCL,
  freqHz: 400000,
});

print(JSON.stringify(bus.status()));
print(JSON.stringify(bus.scan()));
bus.write(0x3c, [0x00, 0xae]);
var id = bus.writeRead(0x68, [0x75], 1);
bus.close();
```

- Keep the top-level `i2c` module as the factory and constants container.
- Let `open(...)` return an `I2CBus` object instead of only mutating one global singleton.
- Keep explicit `close()` as the primary lifecycle boundary even if the runtime also uses GC finalizers as a fallback to release leaked native handles.
- Gate the module behind `FEATURE_I2C`.

Recommended stable `I2CBus` methods:

- `status()`
- `close()`
- `scan()`
- `write(addr, data)`
- `writeChunks(addr, chunks)`
- `read(addr, length)`
- `writeRead(addr, writeData, readLength)`

Remaining freeze work:

- Add or keep focused JS tests for stale-handle behavior, chunk writes, and feature-disabled boards.
- Keep [docs/c-api.md](c-api.md) and [types/esp32qjs-js-api.d.ts](../types/esp32qjs-js-api.d.ts) aligned with the bus-object surface.

Intentional non-goals:

- no register map helpers
- no sensor drivers
- no device cache abstraction at the JS API layer unless later proven necessary

### `spi`

Status: `Candidate for freeze`

The bus/device object model is now in place and deliberately mirrors I2C before higher-level device protocols are added in JS.

Recommended stable target:

- Keep the top-level `spi` module as the factory and constants container.
- Let `spi.openBus(options?)` return an `SPIBus`, using board/profile default pin constants when no pin override is supplied.
- Let `SPIBus.openDevice(...)` return an `SPIDevice`.
- Keep the first stable surface synchronous and explicit:
  `SPIDevice.transfer(...)`, `SPIDevice.write(...)`, `SPIDevice.read(...)`
- Keep `SPIDevice.writeChunks(...)` for byte-source chunk arrays and `SPIDevice.writeSource(...)` for retained native byte span sources.
- Keep explicit `close()` as the primary lifecycle boundary on both `SPIBus` and `SPIDevice`, even if the runtime also uses GC finalizers as a fallback.
- Gate the module behind `FEATURE_SPI`.
- Keep same-board loopback test coverage available through the remote test harness so SPI data-path regressions are caught without requiring a dedicated SPI peripheral.
- Keep display flushes layered above SPI. Native display helpers may produce byte chunks or byte span sources, but SPI should only consume generic byte payload abstractions.

Intentional non-goals for the first stable SPI layer:

- no command/address phase helpers
- no queued async transactions
- no protocol-specific flash/display helpers in the native layer

Remaining freeze work:

- Keep loopback coverage for `transfer`, `write`, and `read`.
- Keep display flush coverage for `writeSource(...)` and compatibility coverage for `writeChunks(...)`.
- Document any DMA staging behavior as implementation detail, not as a JS-visible display shortcut.

### `uart`

Status: `Candidate for freeze`

UART follows the same object-handle direction as I2C and SPI, but remains a synchronous TTL peripheral API rather than a `Stream`.

Recommended stable target:

- Keep the top-level `uart` module as the factory and constants container.
- Let `uart.open(options?)` return a `UARTPort`, using board/profile default `port`, `tx`, `rx`, and `baud` values when no override is supplied.
- Keep the first stable surface synchronous and explicit:
  `UARTPort.write(...)`, `UARTPort.writeChunks(...)`, `UARTPort.writeSource(...)`, `UARTPort.read(...)`, `UARTPort.available()`, `UARTPort.flush(...)`, and `UARTPort.clearRx()`.
- Keep `write(...)` on `ByteSource`, `writeChunks(...)` on chunk arrays, and `writeSource(...)` on generic `ByteSpanSource`; UART should not inspect display-buffer internals.
- Keep explicit `close()` as the primary lifecycle boundary and stale-handle errors after close.
- Gate the module behind `FEATURE_UART`.

Intentional non-goals for the first stable UART layer:

- no Stream adapter yet
- no hardware RTS/CTS flow control
- no RS485 mode
- no pattern detection or event queue surface

### `displayBuffer`

Status: `Candidate for freeze`

The old display buffer plan has been implemented and the remaining active surface is now part of the host API reference.

What is already good:

- `displayBuffer` is feature-gated and reported through `sys.info().features.displayBuffer`.
- The module exposes native `mono1` and `rgb565` buffers, dirty bounds, drawing primitives, EQF1 fixed bitmap font loading, and byte-view rectangle export.
- `readRect(...)` and `readRectChunks(...)` return generic native byte sources; `createSpanSource(...)` returns a retained `DisplayBufferSpanSource` whose display-only `setRect(...)` control is separate from the generic `ByteSpanSource` consumed by SPI.
- The layered JavaScript display library separates framebuffer rendering, panel command sequencing, presentation policy, and SPI/I2C transport ownership.

Recommended stable target:

- Keep `displayBuffer` as a low-level pixel buffer and byte-export primitive, not a panel driver.
- Keep JS libraries responsible for ST7789, SSD1306, double-buffer policy, animation timing, and widget behavior.
- Keep the native text API intentionally simple. UTF-8 or Chinese display should continue to use JS-side mapped fonts unless a measured workload proves a native text shaper is needed.
- Treat storage and DMA behavior as buffer/export options, not as SPI-specific shortcuts.

Remaining freeze work:

- Keep tests for close/finalizer behavior, dirty bounds, drawing primitives, font loading, and byte-source chunk exports.
- Add future formats such as grayscale only when a concrete display driver needs them.
- Consider `copyFrom(...)`, `blitFrom(...)`, multi-rect dirty tracking, or native clipping only after profiling shows the current bounding-rect path is limiting real UI workloads.

### `dac`

Status: `Adjust before freeze`

This module now exists, but its first stable shape should stay intentionally small.

Hardware support note:

- Local ESP-IDF capability headers show DAC support on `esp32` and `esp32s2`.
- Current `esp32c3` and `esp32s3` targets do not expose DAC hardware support.

For cross-board stability, the proposed API is:

- gate `dac` behind `FEATURE_DAC`
- make `FEATURE_DAC` depend on `SOC_DAC_SUPPORTED`
- expose compile-time availability through `sys.info().features.dac`
- do not register the module at all on boards where the feature is disabled

Recommended first stable scope: oneshot DAC only.

Proposed shape:

```js
// Mapping and limits
dac.CHANNEL_COUNT
dac.RESOLUTION_BITS
dac.MAX_VALUE
dac.CHANNEL_0
dac.CHANNEL_1
dac.status(channel?)
dac.ioToChannel(pin)
dac.channelToIo(channel)

// Oneshot control
dac.open(channel)
dac.close(channel)
dac.write(channel, value)
```

Recommended status object:

```js
{
  channel: 0,
  opened: true,
  pin: 25,
  resolutionBits: 8,
  maxValue: 255,
  lastValue: 128
}
```

Recommended rules:

- `write(channel, value)` should take the raw digital DAC value `0..MAX_VALUE`.
- No voltage unit conversion should be built in.
- No waveform helpers should be built into the first version.
- The whole module should be absent unless the board profile enables `FEATURE_DAC`.

What is intentionally deferred:

- cosine generator support
- continuous/DMA output
- buffered streaming

If those are needed later, they should be added as separate explicit surfaces rather than overloading the basic oneshot module.

## Connectivity Modules

### `wifi`

Status: `Adjust before freeze`

What is good:

- `status()`, `scan()`, `connect()`, `disconnect()` are the right primitives.
- The current ESP event integration is a useful native future-driver backend.

What should stay intentional:

- `wifi.status()` should remain the single authoritative status shape.
- `wifi.scan()` and `wifi.connect(...)` remain the only module-level method
  shapes and use the future scheduler internally.
- `Future.call(wifi.scan, wifi, [])` and the equivalent connect call provide
  concurrent use without a `wifi.async` namespace.
- Any future reconnect, AP mode, hostname mutation, or event subscription work should be introduced carefully, not mixed into the basic station API casually.

Recommended stable baseline:

- `DEFAULT_TIMEOUT_MS`
- `status()`
- `scan()`
- `connect(ssid, password, timeoutMs?)`
- `disconnect()`
- Gate the module behind `FEATURE_WIFI`.

### `http`

Status: `Adjust before freeze`

What is good:

- `fetch(...)` and `http.server(...)` are now separately gateable instead of being forced behind one feature.
- `Request`, `Response`, `Headers`, `HttpServer`, and `StaticFileHandler` already compose well.

What should stay intentional:

- Keep the surface intentionally smaller than browser Fetch or Express.
- Keep only synchronous `fetch(...)` / `http.fetch(...)` at module level and
  expose their existing worker backend through `Future.call`.
- Do not overload `fetch(...)` with an optional callback and do not retain
  `http.async`.
- Treat this as an embedded `Future` API, not a Promise compatibility layer.
- Replace native server route callbacks with declarative routes and a bounded
  request `EventQueue`; implement routing policy in JS.

Recommended stable baseline:

- `fetch(url, init?)`
- `http.fetch(...)` as the same transport primitive
- `http.server(options?)`
- `http.staticFileHandler(root)`
- `HttpServer.route(method, path)`, `start()`, `receive(timeoutMs?)`,
  `respond(request, response)`, and `stop()`
- `StaticFileHandler.handle(request)`
- Gate the client transport behind `FEATURE_HTTP`.
- Gate the server transport behind `FEATURE_HTTP_SERVER`.
- Gate `staticFileHandler(...)` behind the composite capability `FEATURE_HTTP_SERVER && FEATURE_FS`.

Future rule:

- Middleware stacks, sessions, templates, and web-framework features belong in JS libraries, not in the native `http` module.

## JS-Side Libraries

### `display` and `ui`

Status: `Versioned JS libraries, not firmware ABI`

Recommendation:

- Keep these documented, but version them as JS libraries layered on top of stable host APIs.
- They should be free to evolve faster than the built-in firmware API.
- Their compatibility should be handled by JS library versioning, not by freezing them as low-level host ABI.

## Naming and Compatibility Rules

Before calling the built-in host API stable, adopt these rules:

- Use `i2c`, not `iic`.
- Prefer ESP-IDF peripheral names over Arduino compatibility aliases.
- Keep module methods callback-free; repeated input such as GPIO interrupts uses
  `EventQueue`, and concurrency is composed through `Future`.
- Do not add broad alias sets for every peripheral API. One clear name is better than many compatibility names.
- New optional modules should always be introduced as named build features with board-level defaults, not as unconditional globals.

## Freeze Order

Recommended order for stabilization:

1. Freeze now:
   `help/load/timers`, `fs`, `Stream`, `Headers`, `Request`, `Response`, `sys`, `adc`
2. Candidate for freeze after focused validation:
   `nvs`, `i2c`, `spi`, `uart`, `displayBuffer`
3. Adjust before freeze:
   `Future`, `EventQueue`, `gpio`, `wifi`, `http`, `ledc`, `dac`

## Immediate Next Steps

Recommended implementation order from this plan:

1. Tighten `ledc` status semantics so status objects never imply configuration that did not happen, then decide whether low-speed mode is intentionally fixed for the first stable API.
2. Validate `dac` lifecycle and status on an `esp32` or `esp32s2` board before calling the module stable.
3. Finish freeze coverage for the current byte payload paths: `DisplayBuffer.createSpanSource(...)`, `SPIDevice.writeSource(...)`, `displayBuffer.readRectChunks(...)`, `SPIDevice.writeChunks(...)`, and `I2CBus.writeChunks(...)`.
4. Keep `i2c`, `spi`, `uart`, `displayBuffer`, `wifi`, and `http` reference docs and TypeScript definitions aligned with their current implementation before marking them stable.
5. Improve JS display demo smoke coverage so native drawing, mapped fonts, transparent text, dirty bounds, and chunked flushes are exercised together.
6. Consider `displayBuffer` follow-up features only when a measured workload needs them: grayscale formats, `copyFrom(...)`, `blitFrom(...)`, multi-rect dirty tracking, or native clipping.
