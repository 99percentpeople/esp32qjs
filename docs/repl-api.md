# REPL API Reference

This project exposes a small JavaScript REPL on the XIAO ESP32-S3. Run `help()` on the device to point back to this document.

## Global Helpers

- `help()`
  Print the location of this document.
- `print(...values)`
  Write values to the serial console.
- `gc()`
  Run the JavaScript garbage collector.
- `fetch(url, options?)`
  Run a blocking HTTP request and return a response object.
- `fetch(url, callback)` / `fetch(url, options, callback)`
  Run an asynchronous HTTP request and call `callback(error, response)` on completion.
- `load(path)`
  Evaluate a script from LittleFS. Relative paths resolve under `/littlefs`, and paths cannot escape that root.
- `sleep(ms)` / `delay(ms)`
  Block the REPL task for `ms` milliseconds.

Startup behavior:

- If `/littlefs/index.js` exists, it is loaded automatically before the first `js>` prompt appears.
- This is the recommended place for board startup logic such as `wifi.connect(...)`.

Examples:

```js
print("hello");
gc();
sleep(50);
print(fetch("https://example.com").status);
load("demo.js");
```

Example `index.js`:

```js
print("[startup] boot script running");
wifi.connect("your-ssid", "your-password");
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

- `LED_BUILTIN`
  Built-in LED pin number, `21`.
- `SCRIPTS_DIR`
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

## `gpio` Module

- `gpio.INPUT`
  Input mode string for `gpio.pinMode()`.
- `gpio.OUTPUT`
  Output mode string for `gpio.pinMode()`.
- `gpio.LED_BUILTIN`
  Built-in LED pin number, `21`.
- `gpio.USER_LED_PIN`
  XIAO ESP32-S3 user LED pin number.
- `gpio.USER_LED_ACTIVE_LOW`
  `true` because the board LED is active-low.
- `gpio.pinMode(pin, mode)`
  Configure a GPIO as `gpio.INPUT` or `gpio.OUTPUT`.
- `gpio.digitalWrite(pin, value)`
  Set a GPIO output level.
- `gpio.digitalRead(pin)`
  Read a GPIO level and return `true` or `false`.
- `gpio.led(value)`
  Control the XIAO ESP32-S3 user LED. `true` turns it on.

Example:

```js
gpio.pinMode(gpio.LED_BUILTIN, gpio.OUTPUT);
gpio.led(true);
sleep(100);
gpio.led(false);
```

## `esp32` Module

- `esp32.info()`
  Return board/chip identity plus memory/runtime fields:
  `{ board, chip, userLedPin, userLedActiveLow, scriptsDir, flashSize, psramEnabled, psramSize, freePsram, totalInternalHeap, freeInternalHeap, jsHeapSize, jsHeapRegion, littlefsMounted, autoRunIndexJs, formatLittlefsOnMountFail, freeHeap, jsTimeMs }`.
- `esp32.millis()`
  Return monotonic milliseconds from `esp_timer`.
- `esp32.micros()`
  Return monotonic microseconds from `esp_timer`.
- `esp32.freeHeap()`
  Return current free heap in bytes.

Example:

```js
print(esp32.info());
print(esp32.millis());
print(esp32.freeHeap());
```

## `wifi` Module

Wi-Fi credentials are kept in RAM. Rebooting the board clears the active station config.

- `wifi.DEFAULT_TIMEOUT_MS`
  Default station connect timeout in milliseconds, `15000`.
- `wifi.status()`
  Return `{ initialized, started, connected, scanning, ssid, hostname, ip, netmask, gateway, lastDisconnectReason }`.
- `wifi.connect(ssid, password, timeoutMs = wifi.DEFAULT_TIMEOUT_MS)`
  Start station mode, connect to an AP, and return the updated status object.
- `wifi.disconnect()`
  Disconnect the station and return the updated status object.
- `wifi.scan()`
  Run a blocking AP scan and return an array of `{ ssid, bssid, rssi, channel, authMode, hidden }`.
- `wifi.scan(callback)`
  Start a non-blocking scan and call `callback(results)` after the scan completes.

Example:

```js
print(JSON.stringify(wifi.status()));
const aps = wifi.scan();
print(aps.length);
wifi.scan(function (results) { print("async scan", results.length); });
wifi.connect("your-ssid", "your-password");
print(JSON.stringify(wifi.status()));
wifi.disconnect();
```

## `http` Module

- `http.DEFAULT_TIMEOUT_MS`
  Default request timeout in milliseconds, `15000`.
- `http.MAX_RESPONSE_BYTES`
  Maximum response body captured into memory, `32768`.
- `http.fetch(url, options?)`
  Run a blocking HTTP request and return a response object.
- `http.fetch(url, callback)` / `http.fetch(url, options, callback)`
  Run an asynchronous HTTP request and call `callback(error, response)`.

Supported `options` fields:

- `method`
  HTTP method string such as `"GET"`, `"POST"`, `"PUT"`, `"PATCH"`, `"DELETE"`, `"HEAD"`, or `"OPTIONS"`.
- `headers`
  Plain object of request headers.
- `body`
  UTF-8 string request body.
- `timeoutMs`
  Per-request timeout in milliseconds.

Response object shape:

- `ok`
  `true` for HTTP 2xx.
- `status`
  Numeric HTTP status code.
- `statusText`
  Short status text when known.
- `url`
  Final request URL reported by the client.
- `body`
  UTF-8 response body, truncated at `http.MAX_RESPONSE_BYTES`.
- `headers`
  Plain object of captured response headers.
- `contentLength`
  Server-reported content length, or `0`/`-1` when not available.
- `truncated`
  `true` when the body exceeded the in-memory capture limit or capture switched to a partial response under memory pressure.

Examples:

```js
var response = fetch("http://example.com");
print(response.status, response.ok, response.body.length);

fetch("https://example.com", function (error, response) {
  print(error === null, response.status);
});

var head = http.fetch("http://example.com", {
  method: "HEAD",
  timeoutMs: 5000,
});
print(head.status, head.body.length);
```
