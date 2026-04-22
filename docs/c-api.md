# C API Reference

This document covers the APIs exported directly by the firmware runtime.

## Global Helpers

- `help()`
  Print the location of these API documents.
- `print(...values)`
  Write values to the serial console.
- `gc()`
  Run the JavaScript garbage collector.
- `defer()`
  Create a deferred helper object for callback-style async work.
- `fetch(input, options?)`
  Run a blocking HTTP request and return a `Response`.
- `fetch(input, callback)` / `fetch(input, options, callback)`
  Run an asynchronous HTTP request and call `callback(response, error)` on completion.
- `load(path)`
  Evaluate a script from LittleFS. Relative paths resolve under `/littlefs`, and paths cannot escape that root.
- `sleep(ms)` / `delay(ms)`
  Block the REPL task for `ms` milliseconds.
- `waitFor(start, timeoutMs?)`
  Run `start(resolve, reject, deferred)` and block while the runtime keeps pumping timers, Wi-Fi, and HTTP callbacks. Return the resolved value or throw the rejection.

Startup behavior:

- If `/littlefs/index.js` exists, it is loaded automatically before the first `js>` prompt appears.
- `index.js` is the single startup entry point. Use it to `load(...)` other scripts, drivers, and app code.
- This is the recommended place for board startup logic such as `load("_sys/display.js")`, `load("_sys/ui.js")`, and `wifi.connect(...)`.

Examples:

```js
print("hello");
gc();
sleep(50);
print(fetch("https://example.com").status);
load("demo.js");
print(waitFor(function (resolve) {
  setTimeout(function () { resolve(123); }, 50);
}, 1000));
```

Example `index.js`:

```js
print("[startup] boot script running");
load("_sys/display.js");
load("_sys/ui.js");
wifi.connect("your-ssid", "your-password", function (status, error) {
  print(error === undefined, status && status.ip);
});
```

## Deferred Helpers

- `defer()`
  Return `{ settled, done, ok, value, error, resolve, reject, callback, wait }`.
- `deferred.resolve(value)`
  Resolve the deferred with `value`.
- `deferred.reject(error)`
  Reject the deferred with `error`.
- `deferred.callback(value, error?)`
  Unified callback helper. Resolve with `value` when `error` is empty, otherwise reject with `error`.
- `deferred.wait(timeoutMs?)`
  Block until the deferred settles, while still polling async host events.

Examples:

```js
var d = defer();
setTimeout(function () { d.resolve("ok"); }, 50);
print(d.wait(1000));

var aps = waitFor(function (resolve, reject, deferred) {
  wifi.scan(deferred.callback);
}, 10000);
print(aps.length);

var response = waitFor(function (resolve, reject, deferred) {
  fetch("https://example.com", deferred.callback);
}, 10000);
print(response.status);
```

## Timers

- `setTimeout(fn, ms)`
  Run `fn` once after `ms` milliseconds.
- `clearTimeout(id)`
  Cancel a timeout timer.
- `setInterval(fn, ms)`
  Run `fn` repeatedly every `ms` milliseconds.
- `clearInterval(id)`
  Cancel an interval timer.

Example:

```js
const id = setInterval(function () { print("tick"); }, 500);
clearInterval(id);
```

## Constants

- `SCRIPTS_DIR`
  Exposed only when the `fs` feature is compiled in.
  Script base directory, `"/littlefs"`.

## `fs` Module

All `fs` operations are restricted to `/littlefs`.

- `fs.ROOT`
  File-system root, `"/littlefs"`.
- `fs.list(path = ".")`
  Return an array of entries for a directory.
- `fs.stat(path)`
  Return `{ name, path, isDir, size }` for a path.
- `fs.exists(path)`
  Return `true` if the path exists.
- `fs.readText(path)`
  Read a UTF-8 text file.
- `fs.open(path, mode?)`
  Open a file stream. Supported modes are `r`, `rb`, `w`, `wb`, `a`, `ab`, `r+`, `w+`, and `a+`.
- `fs.writeText(path, text)`
  Overwrite a text file and return the number of bytes written.
- `fs.appendText(path, text)`
  Append text and return the number of bytes written.
- `fs.mkdir(path)`
  Create one directory level.
- `fs.rename(fromPath, toPath)`
  Rename a file or directory.
- `fs.remove(path)`
  Remove a file or an empty directory.

Example:

```js
fs.writeText("notes.txt", "hello\n");
print(fs.readText("notes.txt"));
print(fs.stat("notes.txt"));
print(JSON.stringify(fs.list(".")));
fs.rename("notes.txt", "notes-old.txt");
fs.remove("notes-old.txt");
```

## `Stream` Type

`fs.open()` returns a `Stream`. `Response.body`, `Request.body`, and `Response.stream(...)` also use the same stream interface.

- `Stream.SEEK_SET`
- `Stream.SEEK_CUR`
- `Stream.SEEK_END`
  Seek constants for `stream.seek(...)`.

Stream instance shape:

- `kind`
  Stream kind. File streams currently report `"file"`.
- `path`
  Full LittleFS path for file-backed streams, for example `"/littlefs/notes.txt"`.
- `mode`
  The mode string passed to `fs.open(...)`.
- `readable`
- `writable`
- `read(size?)`
  Read up to `size` bytes as a UTF-8 string. Returns `null` at EOF. Default chunk size is `1024`.
- `write(text)`
  Write a string and return the written byte count.
- `flush()`
- `close()`
- `seek(offset, whence?)`
  Move the file cursor and return the new position.
- `tell()`
  Return the current file cursor position.
- `eof()`
  Return `true` once the stream reached EOF.

Example:

```js
var stream = fs.open("_sys/ui/core.js", "r");
print(stream.tell());
print(JSON.stringify(stream.read(32)));
stream.seek(0, Stream.SEEK_SET);
stream.close();
```

## `Headers`, `Request`, and `Response`

- `new Headers(init?)`
  Create a header collection from a plain object or another `Headers`.
- `new Request(input, init?)`
  Create a request from a URL string or another `Request`.
- `new Response(body?, init?)`
  Create a response from a string, `Stream`, or omitted body.
- `Response.text(text, init?)`
- `Response.json(value, init?)`
- `Response.stream(stream, init?)`

`Headers` methods:

- `headers.get(name)`
- `headers.set(name, value)`
- `headers.has(name)`
- `headers.delete(name)`
- `headers.entries()`
- `headers.toObject()`

`Request` shape:

- `method`
- `url`
- `path`
- `queryString`
- `query`
- `route`
  The matched route pattern as a string.
- `mountPath`
  The mounted path prefix computed by the router, primarily for middleware.
- `relativePath`
  The path after removing the mounted prefix, primarily for file-serving middleware.
- `headers`
  A `Headers` object.
- `body`
  A `Stream`.
- `text()`
- `json()`

`Response` shape:

- `ok`
- `status`
- `statusText`
- `url`
- `headers`
  A `Headers` object.
- `body`
  A `Stream`.
- `text()`
- `json()`

Body consumption notes:

- `Request.text()` / `Request.json()` read the full request body and consume the underlying stream.
- `Response.text()` / `Response.json()` read the full response body and consume the underlying stream.
- If you read directly from `request.body` or `response.body`, close the stream manually when you are done.

Examples:

```js
var request = new Request("https://example.com", {
  method: "POST",
  headers: { "content-type": "application/json" },
  body: JSON.stringify({ hello: "world" }),
});

var response = fetch(request);
print(response.status, response.ok);
print(response.text().length);

var headers = new Headers({ "content-type": "text/plain; charset=utf-8" });
print(headers.get("content-type"));
```

## `i2c` Module

- `i2c.DEFAULT_SDA`
  Default SDA pin from Kconfig.
- `i2c.DEFAULT_SCL`
  Default SCL pin from Kconfig.
- `i2c.DEFAULT_FREQ_HZ`
  Default bus speed, `400000`.
- `i2c.DEFAULT_TIMEOUT_MS`
  Default transfer timeout in milliseconds.
- `i2c.open(options?)`
  Open the shared I2C master bus. `options` can include `{ sda, scl, freqHz, timeoutMs, internalPullup }`.
- `i2c.close()`
  Close the active I2C bus.
- `i2c.status()`
  Return `{ opened, sda, scl, freqHz, timeoutMs, internalPullup }`.
- `i2c.scan()`
  Probe `0x03..0x77` and return an array of 7-bit device addresses.
- `i2c.write(addr, data)`
  Write an array-like sequence of bytes and return the number of bytes written.
- `i2c.read(addr, length)`
  Read `length` bytes and return them as a JavaScript array.
- `i2c.writeRead(addr, writeData, readLength)`
  Write bytes, then read bytes in one transaction and return the read array.

Example:

```js
print(JSON.stringify(i2c.open({ sda: 5, scl: 6, freqHz: 400000 })));
print(JSON.stringify(i2c.scan())); // [60] for an SSD1306 at 0x3c
print(i2c.write(0x3c, [0x00, 0xAF])); // SSD1306 display on
```

## `gpio` Module

- `gpio.DISABLED`
  Disabled mode string for `gpio.pinMode()` or `gpio.configure()`.
- `gpio.INPUT`
  Input mode string for `gpio.pinMode()`.
- `gpio.OUTPUT`
  Output mode string for `gpio.pinMode()`.
- `gpio.INPUT_OUTPUT`
  Input + output mode string for bidirectional pins.
- `gpio.OUTPUT_OPEN_DRAIN`
  Open-drain output mode string.
- `gpio.INPUT_OUTPUT_OPEN_DRAIN`
  Open-drain input + output mode string.
- `gpio.FLOATING`
  Disable internal pulls.
- `gpio.PULLUP`
  Enable the internal pull-up.
- `gpio.PULLDOWN`
  Enable the internal pull-down.
- `gpio.PULLUP_PULLDOWN`
  Enable both internal pulls when supported by the pad.
- `gpio.CHANGE`, `gpio.RISING`, `gpio.FALLING`
  Edge-trigger constants accepted by `gpio.attachInterrupt(...)`.
- `gpio.LOW`, `gpio.HIGH`
  Numeric helpers (`0` / `1`) used both for output levels and level-trigger interrupt modes in `gpio.attachInterrupt(...)`.
- `gpio.DRIVE_0` .. `gpio.DRIVE_3`
  Drive-strength levels accepted by `gpio.setDriveStrength()` and `gpio.configure()`.
- `gpio.LED_BUILTIN`
  Built-in LED pin number for the current board.
- `gpio.USER_LED_PIN`
  User LED pin number for the current board.
- `gpio.USER_LED_ACTIVE_LOW`
  Whether the board LED is active-low.
- `gpio.isValid(pin)`
  Return `true` when `pin` is a usable digital GPIO on the current target.
- `gpio.isOutputCapable(pin)`
  Return `true` when the pad supports output mode, drive strength, and hold.
- `gpio.pinMode(pin, mode)`
  Configure a GPIO as `gpio.DISABLED`, `gpio.INPUT`, `gpio.OUTPUT`, `gpio.INPUT_OUTPUT`, `gpio.OUTPUT_OPEN_DRAIN`, or `gpio.INPUT_OUTPUT_OPEN_DRAIN`.
- `gpio.setPull(pin, mode)`
  Set internal pull resistors with `gpio.FLOATING`, `gpio.PULLUP`, `gpio.PULLDOWN`, or `gpio.PULLUP_PULLDOWN`.
- `gpio.status(pin)`
  Return the live pad configuration object:
  `{ pin, valid, outputCapable, mode, pull, level, inputEnabled, outputEnabled, openDrain, pullup, pulldown, driveStrength, held, functionSelect, signalOut, outputControlledByPeripheral, outputEnableInverted, sleepEnabled, interruptAttached, interruptMode, interruptDropped }`.
- `gpio.configure(pin, options)`
  Apply `{ mode, pull, level, driveStrength, hold }` in one call and return `gpio.status(pin)`.
- `gpio.digitalWrite(pin, value)`
  Set a GPIO output level. If the pin is not already output-enabled, the helper promotes it to `gpio.OUTPUT`.
- `gpio.digitalRead(pin)`
  Read a GPIO level and return `true` or `false`.
- `gpio.toggle(pin)`
  Flip the current output level and return the new boolean level.
- `gpio.getDriveStrength(pin)`
  Return the current numeric drive strength (`0..3`).
- `gpio.setDriveStrength(pin, strength)`
  Update the pad drive strength and return the applied numeric value.
- `gpio.hold(pin, enabled)`
  Enable or disable pad hold on output-capable GPIOs.
- `gpio.attachInterrupt(pin, callback, mode = gpio.CHANGE)`
  Attach an Arduino-style GPIO interrupt callback. The ESP-IDF ISR only queues an event; the JavaScript callback runs later on the JS thread and receives `{ pin, level, mode }`.
- `gpio.detachInterrupt(pin)`
  Remove the interrupt callback from a pin and return `gpio.status(pin)`.
- `gpio.reset(pin)`
  Reset the pad back to the ESP-IDF default GPIO state.
- `gpio.led(value)`
  Control the board user LED. `true` turns it on.

Examples:

Configure an output and toggle it:

```js
var pin = gpio.USER_LED_PIN >= 0 ? gpio.USER_LED_PIN : gpio.LED_BUILTIN;

// Make the LED pin a push-pull output with a known initial level.
print(gpio.isValid(pin), gpio.isOutputCapable(pin));
print(JSON.stringify(gpio.configure(pin, {
  mode: gpio.OUTPUT,
  pull: gpio.FLOATING,
  driveStrength: gpio.DRIVE_1,
  level: gpio.HIGH,
})));

// Flip the output and read back the new boolean level.
sleep(100);
print(gpio.toggle(pin)); // false
gpio.led(false);
```

Read an input with pull-up enabled:

```js
var buttonPin = 9;

// Typical button wiring uses INPUT + pull-up and reads LOW when pressed.
gpio.pinMode(buttonPin, gpio.INPUT);
gpio.setPull(buttonPin, gpio.PULLUP);

print("button level:", gpio.digitalRead(buttonPin));
print(JSON.stringify(gpio.status(buttonPin)));
```

Attach an interrupt and handle it in JavaScript:

```js
var buttonPin = 9;
var interruptCount = 0;

gpio.pinMode(buttonPin, gpio.INPUT);
gpio.setPull(buttonPin, gpio.PULLUP);

// The callback runs on the JS thread, not directly inside the ISR.
// You can ignore the event argument if you only want Arduino-style behavior.
gpio.attachInterrupt(buttonPin, function (event) {
  interruptCount++;
  print("interrupt", interruptCount, event.pin, event.mode, event.level);
}, gpio.FALLING);

// ... your app logic here ...

// Detach when the pin is no longer needed.
gpio.detachInterrupt(buttonPin);
```

Use level-triggered interrupts explicitly:

```js
var pin = 9;

gpio.pinMode(pin, gpio.INPUT);
gpio.setPull(pin, gpio.PULLDOWN);

// LOW/HIGH reuse the same 0/1 constants as digital levels.
gpio.attachInterrupt(pin, function (event) {
  print("level interrupt", event.mode, event.level);
}, gpio.HIGH);
```

## `ledc` Module

This module exposes the ESP-IDF LEDC low-level timer/channel primitives. It does not implement higher-level drivers such as servos or `analogWrite(...)`.

- `ledc.AUTO_CLOCK`, `ledc.APB_CLOCK`, `ledc.XTAL_CLOCK`, `ledc.RC_FAST_CLOCK`
  Clock-source strings accepted by `ledc.timerConfig(...)`.
- `ledc.SLEEP_NO_ALIVE_NO_PD`, `ledc.SLEEP_NO_ALIVE_ALLOW_PD`, `ledc.SLEEP_KEEP_ALIVE`
  Sleep-mode strings accepted by `ledc.channelConfig(...)`.
- `ledc.CHANNEL_COUNT`
  Number of LEDC channels on the active target.
- `ledc.TIMER_COUNT`
  Number of LEDC timers on the active target.
- `ledc.MAX_DUTY_RESOLUTION_BITS`
  Maximum duty-resolution bits supported by the active target.
- `ledc.timerConfig(timer, options)`
  Configure or deconfigure one timer. `options` accepts `{ freqHz, dutyResolution, clock, deconfigure }`.
- `ledc.channelConfig(channel, options)`
  Configure or deconfigure one channel. `options` accepts `{ pin, timer, duty, hpoint, outputInvert, sleepMode, deconfigure }`.
- `ledc.timerStatus(timer)`
  Return `{ timer, configured, paused, freqHz, dutyResolution, maxDuty, clock }`.
- `ledc.channelStatus(channel)`
  Return `{ channel, configured, pin, timer, duty, hpoint, maxDuty, outputInvert, sleepMode }`.
- `ledc.setDuty(channel, duty)`
- `ledc.setDutyWithHpoint(channel, duty, hpoint)`
- `ledc.setDutyAndUpdate(channel, duty, hpoint?)`
  Update duty/hpoint state and return `ledc.channelStatus(channel)`.
- `ledc.updateDuty(channel)`
  Apply pending duty changes to hardware and return `ledc.channelStatus(channel)`.
- `ledc.getDuty(channel)`
- `ledc.getHpoint(channel)`
  Read live channel state from the driver.
- `ledc.setFreq(timer, freqHz)`
  Update timer frequency and return `ledc.timerStatus(timer)`.
- `ledc.getFreq(timer)`
  Read the live timer frequency.
- `ledc.bindChannelTimer(channel, timer)`
  Rebind a channel to another timer and return `ledc.channelStatus(channel)`.
- `ledc.stop(channel, idleLevel = false)`
  Stop PWM output on a channel.
- `ledc.timerPause(timer)` / `ledc.timerResume(timer)`
  Pause or resume a timer and return `ledc.timerStatus(timer)`.

Example:

```js
var pin = gpio.USER_LED_PIN >= 0 ? gpio.USER_LED_PIN : gpio.LED_BUILTIN;

ledc.timerConfig(0, { freqHz: 5000, dutyResolution: 8 });
ledc.channelConfig(0, {
  pin: pin,
  timer: 0,
  duty: 0,
  sleepMode: ledc.SLEEP_NO_ALIVE_NO_PD,
});

for (var duty = 0; duty <= 255; duty += 32) {
  ledc.setDutyAndUpdate(0, duty);
  sleep(40);
}

ledc.stop(0, false);
ledc.channelConfig(0, { deconfigure: true });
ledc.timerConfig(0, { deconfigure: true });
```

## `adc` Module

This module exposes ESP-IDF ADC oneshot primitives and GPIO/channel mapping helpers. It does not implement board-specific sensor drivers.

- `adc.UNIT_1`, `adc.UNIT_2`
  ADC unit identifiers accepted by `adc.open(...)`, `adc.status(...)`, and the read/configure helpers.
- `adc.ATTEN_DB_0`, `adc.ATTEN_DB_2_5`, `adc.ATTEN_DB_6`, `adc.ATTEN_DB_12`
  Attenuation constants accepted by `adc.configure(...)`.
- `adc.BITWIDTH_DEFAULT`, `adc.BITWIDTH_9` .. `adc.BITWIDTH_13`
  Bit-width constants accepted by `adc.configure(...)`.
- `adc.UNIT_COUNT`
  Number of ADC units on the active target.
- `adc.MAX_CHANNEL_COUNT`
  Maximum channels available on any ADC unit for the active target.
- `adc.open(unit)`
  Open one ADC unit for oneshot reads and return `adc.status(unit)`.
- `adc.close(unit)`
  Close one ADC unit and release any per-channel calibration state.
- `adc.status(unit)`
  Return `{ unit, opened, channelCount, channels }`, where `channels` contains `{ channel, configured, atten, bitwidth, pin, calibrated }`.
- `adc.configure(unit, channel, options)`
  Configure a channel with `{ atten, bitwidth }` and return `adc.status(unit)`.
- `adc.read(unit, channel)`
  Perform one raw oneshot read and return the integer ADC result.
- `adc.readMilliVolts(unit, channel)`
  Return a calibrated result in mV when calibration is available; otherwise it throws a clear calibration-availability error.
- `adc.ioToChannel(pin)`
  Map a GPIO to `{ unit, channel }` or return `null` when the pad is not ADC-capable.
- `adc.channelToIo(unit, channel)`
  Map a unit/channel pair back to its GPIO number or return `null`.

Example:

```js
var ref = adc.ioToChannel(0);

if (ref) {
  adc.open(ref.unit);
  adc.configure(ref.unit, ref.channel, {
    atten: adc.ATTEN_DB_12,
    bitwidth: adc.BITWIDTH_12,
  });

  print(adc.read(ref.unit, ref.channel));
  try {
    print(adc.readMilliVolts(ref.unit, ref.channel));
  } catch (error) {
    print(error.message || error);
  }

  adc.close(ref.unit);
}
```

## `dac` Module

This module exposes ESP-IDF DAC oneshot primitives and GPIO/channel mapping helpers. It is registered only on boards that compile with the `dac` feature enabled.

- `dac.CHANNEL_0`, `dac.CHANNEL_1`
  DAC channel identifiers accepted by `dac.open(...)`, `dac.close(...)`, `dac.status(...)`, `dac.write(...)`, and `dac.channelToIo(...)`.
- `dac.CHANNEL_COUNT`
  Number of DAC channels on the active target.
- `dac.RESOLUTION_BITS`
  DAC output resolution in raw digital bits.
- `dac.MAX_VALUE`
  Maximum raw value accepted by `dac.write(...)`.
- `dac.open(channel)`
  Open one DAC oneshot channel and return `dac.status(channel)`.
- `dac.close(channel)`
  Close one DAC channel and release its oneshot handle.
- `dac.status(channel?)`
  Return one channel status object or, when called with no arguments, an array of all channel statuses.
- `dac.write(channel, value)`
  Output one raw DAC value `0..dac.MAX_VALUE` and return `dac.status(channel)`.
- `dac.ioToChannel(pin)`
  Map a GPIO to `{ channel, pin }` or return `null` when the pad is not DAC-capable.
- `dac.channelToIo(channel)`
  Map a DAC channel back to its GPIO number.

Status objects look like:

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

Example:

```js
var ref = dac.ioToChannel(25);

if (ref) {
  dac.open(ref.channel);
  dac.write(ref.channel, 128);
  print(JSON.stringify(dac.status(ref.channel)));
  dac.close(ref.channel);
}
```

## `esp32` Module

- `esp32.info()`
  Return board/chip identity plus memory/runtime fields:
  `{ board, chip, features, userLedPin, userLedActiveLow, scriptsDir, flashSize, psramEnabled, psramSize, freePsram, totalInternalHeap, freeInternalHeap, jsHeapSize, jsHeapRegion, littlefsMounted, autoRunIndexJs, formatLittlefsOnMountFail, freeHeap, jsTimeMs }`.
  `features` is `{ fs, gpio, ledc, adc, dac, i2c, wifi, http, httpServer, staticFileHandler }` and is the stable way to discover which optional host modules or composite helpers were compiled into the firmware for the current board.
- `esp32.millis()`
  Return monotonic milliseconds from `esp_timer`.
- `esp32.micros()`
  Return monotonic microseconds from `esp_timer`.
- `esp32.freeHeap()`
  Return current free heap in bytes.

Example:

```js
print(JSON.stringify(esp32.info().features));
if (esp32.info().features.fs) {
  print(SCRIPTS_DIR);
}
print(esp32.millis());
print(esp32.freeHeap());
```

## `wifi` Module

Wi-Fi credentials are kept in RAM. Rebooting the board clears the active station config.

- `wifi.DEFAULT_TIMEOUT_MS`
  Default station connect timeout in milliseconds.
- `wifi.status()`
  Return `{ initialized, started, connected, scanning, ssid, hostname, ip, netmask, gateway, lastDisconnectReason, lastDisconnectReasonName }`.
- `wifi.connect(ssid, password, timeoutMs = wifi.DEFAULT_TIMEOUT_MS)`
  Start station mode, connect to an AP, and return the updated status object.
- `wifi.connect(ssid, password, callback)` / `wifi.connect(ssid, password, timeoutMs, callback)`
  Start station mode without blocking the REPL and call `callback(status, error)` on completion.
- `wifi.disconnect()`
  Disconnect the station and return the updated status object.
- `wifi.scan()`
  Run a blocking AP scan and return an array of `{ ssid, bssid, rssi, channel, authMode, hidden }`.
- `wifi.scan(callback)`
  Start a non-blocking scan and call `callback(results, error)` after the scan completes.

Example:

```js
print(JSON.stringify(wifi.status()));
const aps = wifi.scan();
print(aps.length);
wifi.scan(function (results, error) {
  print(error === undefined, results.length);
});
wifi.connect("your-ssid", "your-password");
wifi.connect("your-ssid", "your-password", function (status, error) {
  print(error === undefined, status && status.ip);
});
print(JSON.stringify(wifi.status()));
wifi.disconnect();
```

## `http` Module

The `http` namespace is exposed when either the HTTP client feature or the HTTP server feature is enabled. Individual members are still feature-gated.

- `http.DEFAULT_TIMEOUT_MS`
  Default request timeout in milliseconds. Exposed only when `esp32.info().features.http` is enabled.
- `http.server(options?)`
  Create a lightweight HTTP server object backed by `esp_http_server`. Exposed only when `esp32.info().features.httpServer` is enabled.
- `http.fetch(input, options?)`
  Alias of global `fetch(input, options?)`. Exposed only when `esp32.info().features.http` is enabled.
- `http.fetch(input, callback)` / `http.fetch(input, options, callback)`
  Alias of the asynchronous `fetch(...)` forms. Exposed only when `esp32.info().features.http` is enabled.

Supported `fetch` options:

- `method`
  HTTP method string such as `"GET"`, `"POST"`, `"PUT"`, `"PATCH"`, `"DELETE"`, `"HEAD"`, or `"OPTIONS"`.
- `headers`
  Plain object or `Headers`.
- `body`
  UTF-8 string request body or `Stream`.
- `timeoutMs`
  Per-request timeout in milliseconds.

Examples:

```js
var response = fetch("http://example.com");
print(response.status, response.ok, response.text().length);

fetch("https://example.com", function (response, error) {
  print(error === undefined, response.status, response.text().length);
});

var head = http.fetch("http://example.com", {
  method: "HEAD",
  timeoutMs: 5000,
});
print(head.status, head.text().length);
```

Synchronous `fetch(...)` runs on the same JS thread as `http.server(...)`. If you call your own local server from the same script, prefer the asynchronous form with `waitFor(...)`:

```js
var response = waitFor(function (resolve, reject, deferred) {
  fetch("http://" + wifi.status().ip + ":8080/ping", deferred.callback);
}, 10000);
print(response.text());
```

### HTTP Server API

- `var server = http.server({ port: 8080, host: "0.0.0.0" })`
  Create a server. Default port is `80`. Default host is `"0.0.0.0"` to listen on all interfaces.
- `server.get(pathOrPattern, handler)`
- `server.post(pathOrPattern, handler)`
- `server.put(pathOrPattern, handler)`
- `server.patch(pathOrPattern, handler)`
- `server.delete(pathOrPattern, handler)`
- `server.head(pathOrPattern, handler)`
- `server.options(pathOrPattern, handler)`
- `server.all(pathOrPattern, handler)`
  Register a route handler.
- `http.staticFileHandler(root)` / `staticFileHandler(root)`
  Create a static file handler suitable for routes such as `server.get("/assets/*", staticFileHandler("./www"))`. Exposed only when both `esp32.info().features.httpServer` and `esp32.info().features.fs` are enabled.
- `server.start()`
- `server.stop()`

Handler shape:

- `handler` can be:
  - a function `(req) => Response`
  - or an object with `handle(req)` that returns a `Response`
- `req` is always a `Request`
- route handlers must return a `Response`

Route patterns:

- Exact strings such as `"/ping"` match the request path directly.
- Strings containing `*` use the built-in simple glob matcher, for example `"/assets/*"`.
- You can also pass a `RegExp` directly, for example `/^\\/api\\/v1\\//`.

Host binding:

- Omit `host` or set it to `"0.0.0.0"` to listen on all interfaces.
- Set `host` to a specific local IPv4 address to bind the server to the matching network interface.

`staticFileHandler(root)` behavior:

- `root` must resolve under `/littlefs`.
- Matched file paths are resolved from `req.relativePath`.
- Files are streamed directly from LittleFS in chunks.
- `Response.stream(...)` also streams in chunks and closes the supplied stream after the response is sent.

Example:

```js
var server = http.server({ port: 8080, host: "0.0.0.0" });

server.get("/ping", function (req) {
  return Response.text("pong");
});

server.post("/echo", function (req) {
  return Response.text(req.text(), {
    headers: { "content-type": "text/plain; charset=utf-8" },
  });
});

server.get(/^\/hello$/, function (req) {
  return Response.text(req.query.name || "hello");
});

server.get("/assets/*", staticFileHandler("./_sys"));

server.get("/core", function (req) {
  return Response.stream(fs.open("_sys/display/core.js", "rb"), {
    headers: { "content-type": "application/javascript; charset=utf-8" },
  });
});

server.start();
```
