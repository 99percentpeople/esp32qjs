# REPL API Reference

This project exposes a small JavaScript REPL on the XIAO ESP32-S3. Run `help()` on the device to point back to this document.

## Global Helpers

- `help()`
  Print the location of this document.
- `print(...values)`
  Write values to the serial console.
- `gc()`
  Run the JavaScript garbage collector.
- `defer()`
  Create a deferred helper object for callback-style async work.
- `fetch(url, options?)`
  Run a blocking HTTP request and return a response object.
- `fetch(url, callback)` / `fetch(url, options, callback)`
  Run an asynchronous HTTP request and call `callback(error, response)` on completion.
- `load(path)`
  Evaluate a script from LittleFS. Relative paths resolve under `/littlefs`, and paths cannot escape that root.
- `sleep(ms)` / `delay(ms)`
  Block the REPL task for `ms` milliseconds.
- `waitFor(start, timeoutMs?)`
  Run `start(resolve, reject, deferred)` and block while the REPL keeps pumping timers, Wi-Fi, and HTTP callbacks. Return the resolved value or throw the rejection.

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
wifi.connect("your-ssid", "your-password", function (error, status) {
  print(error === null, status && status.ip);
});
```

## Deferred Helpers

- `defer()`
  Return `{ settled, done, ok, value, error, resolve, reject, callback, nodeCallback, wait }`.
- `deferred.resolve(value)`
  Resolve the deferred with `value`.
- `deferred.reject(error)`
  Reject the deferred with `error`.
- `deferred.callback(value)`
  Convenience callback that resolves with `value`.
- `deferred.nodeCallback(error, value)`
  Node-style callback that rejects on `error` and resolves with `value`.
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
  fetch("https://example.com", deferred.nodeCallback);
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

## `i2c` Module

- `i2c.DEFAULT_SDA`
  Default SDA pin from Kconfig, `5`.
- `i2c.DEFAULT_SCL`
  Default SCL pin from Kconfig, `6`.
- `i2c.DEFAULT_FREQ_HZ`
  Default bus speed, `400000`.
- `i2c.DEFAULT_TIMEOUT_MS`
  Default transfer timeout in milliseconds, `100`.
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

## `display` Helpers

These helpers are implemented in JavaScript on top of the `i2c` module and live under `/littlefs/_sys/display/`. Load `/littlefs/_sys/display.js` from `index.js` or from the REPL before using them.

The display layer is organized around a unified surface interface:

- `display.Surface`
  Base surface contract.
- `display.MonoSurface`
  Generic 1-bit framebuffer surface.
- `display.registerDriver(name, factory)`
  Register a hardware driver factory.
- `display.create(options)` / `display.open(options)`
  Create or initialize a display by driver name. `options.driver` is required.
- `display.listDrivers()`
  Return the registered driver names.

The built-in driver name is `ssd1306`.

Supported `options` fields:

- `sda`, `scl`, `freqHz`, `timeoutMs`, `internalPullup`
  Passed through to `i2c.open(...)` when the bus needs to be configured.
- `address`
  SSD1306 I2C address, default `0x3c`.
- `width`, `height`
  Display size, default `128x64`.
- `spacing`
  Extra inter-character spacing for `drawText()`.

Display instance methods:

- `init()`
  Initialize the panel and clear the framebuffer.
- `clear(enabled = false)` / `fill(enabled)`
  Fill the local framebuffer with off/on pixels.
- `setPixel(x, y, enabled)` / `getPixel(x, y)`
  Read or write one pixel in the framebuffer.
- `fillRect(x, y, width, height, enabled)`
  Fill a rectangle in the framebuffer.
- `drawLine(x0, y0, x1, y1, enabled)`
  Draw a line with Bresenham logic.
- `drawRect(x, y, width, height, enabled)`
  Draw a rectangle outline.
- `drawBitmap(x, y, bitmap, enabled?)`
  Draw a bitmap shaped as `{ width, height, pixels }`.
- `drawChar(x, y, ch, enabled)`
  Draw one glyph using the built-in 5x7 font.
- `drawText(x, y, text, enabled, spacing?)`
  Draw text. Lowercase is normalized to uppercase in the built-in font.
- `measureText(text, style?)`
  Return `{ width, height, lines }` for the built-in 5x7 font.
- `flush()`
  Write the framebuffer to the panel over I2C.
- `on()` / `off()`
  Turn the panel on or off.
- `invert(enabled)`
  Toggle inverse display mode.
- `contrast(value)`
  Set contrast `0..255`.

Example:

```js
var oled = display.open({ driver: "ssd1306", sda: 5, scl: 6, address: 0x3c });
oled.clear();
oled.drawText(0, 0, "HELLO");
oled.drawRect(0, 10, 64, 18, true);
oled.flush();
```

## `ui` Helpers

These helpers are implemented in JavaScript on top of the `display` surface interface and live under `/littlefs/_sys/ui/`. Load `/littlefs/_sys/ui.js` after `/littlefs/_sys/display.js`.

- `ui.box(props?, ...children)`
  Decorated container with optional `padding`, `background`, `border`, `align`, and `valign`.
- `ui.row(props?, ...children)`
  Horizontal layout with optional `gap`, `justify`, `align`, and child `flex`.
- `ui.column(props?, ...children)`
  Vertical layout with optional `gap`, `justify`, `align`, and child `flex`.
- `ui.text(value, props?)`
  Text node drawn with the active surface font.
- `ui.spacer(size | props)`
  Empty layout node for fixed spacing.
- `ui.padding(insets, child, props?)`
  Convenience wrapper that applies padding around a single child.
- `ui.measure(surface, node)`
  Return the natural `{ width, height }` of a node tree.
- `ui.layout(surface, node, options?)`
  Compute node frames without drawing.
- `ui.render(surface, node, options?)`
  Clear, layout, paint, and optionally `flush()` the surface.

Supported common props:

- `width`, `height`
  Fixed outer size in pixels.
- `padding`
  Number or `{ top, right, bottom, left }`.
- `gap`
  Space between row or column children.
- `flex`
  Extra main-axis space share for row or column children.
- `align`
  Cross-axis alignment: `"start"`, `"center"`, `"end"`, or `"stretch"`.
- `justify`
  Main-axis alignment for rows and columns: `"start"`, `"center"`, `"end"`, or `"space-between"`.
- `background`
  Fill the node frame before painting children.
- `border`
  Draw a 1-pixel border around the node frame.
- `color`
  Text color for `ui.text(...)`.

Example:

```js
var oled = display.open({ driver: "ssd1306", sda: 5, scl: 6, address: 0x3c });
var screen = ui.column(
  { padding: 2, gap: 4, border: true },
  ui.text("HELLO"),
  ui.row(
    { gap: 4, align: "center" },
    ui.box({ width: 12, height: 12, border: true }),
    ui.text("WIFI OK")
  )
);

ui.render(oled, screen);
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
  Return `{ initialized, started, connected, scanning, ssid, hostname, ip, netmask, gateway, lastDisconnectReason, lastDisconnectReasonName }`.
- `wifi.connect(ssid, password, timeoutMs = wifi.DEFAULT_TIMEOUT_MS)`
  Start station mode, connect to an AP, and return the updated status object.
- `wifi.connect(ssid, password, callback)` / `wifi.connect(ssid, password, timeoutMs, callback)`
  Start station mode without blocking the REPL and call `callback(error, status)` on completion.
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
wifi.connect("your-ssid", "your-password", function (error, status) {
  print(error === null, status && status.ip);
});
print(JSON.stringify(wifi.status()));
wifi.disconnect();
```

## `http` Module

- `http.DEFAULT_TIMEOUT_MS`
  Default request timeout in milliseconds, `15000`.
- `http.MAX_RESPONSE_BYTES`
  Maximum response body captured into memory, `32768`.
- `http.server(options?)`
  Create a lightweight HTTP server object backed by `esp_http_server`.
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

HTTP server API:

- `var server = http.server({ port: 8080, host: "0.0.0.0" })`
  Create a server. Default port is `80`. Default host is `"0.0.0.0"` to listen on all interfaces.
- `server.get(path, handler)`
- `server.post(path, handler)`
- `server.put(path, handler)`
- `server.patch(path, handler)`
- `server.delete(path, handler)`
- `server.head(path, handler)`
- `server.options(path, handler)`
- `server.all(path, handler)`
  Register a route handler.
- `http.staticFileHandler(root)` / `staticFileHandler(root)`
  Create a handler function suitable for routes such as `server.get("/*", staticFileHandler("./www"))`.
- `server.start()`
- `server.stop()`

Handler shape:

- Input request object:
  - `method`
  - `path`
  - `route`
  - `queryString`
  - `query`
  - `body`
  - `headers`
- Return value:
  - string: sent as a `200` body
  - object: `{ status, headers, body }`
  - `undefined` / `null`: empty `200` response

Route patterns:

- Exact strings such as `"/ping"` match the request path directly.
- Strings containing `*` are compiled into a JavaScript `RegExp` and matched with the runtime's built-in regex engine.
- You can also pass a `RegExp` directly, for example `/^\\/api\\/v1\\//`.

Host binding:

- Omit `host` or set it to `"0.0.0.0"` to listen on all interfaces.
- Set `host` to a specific local IPv4 address to bind the server to the matching network interface.

Example:

```js
var server = http.server({ port: 8080, host: "0.0.0.0" });

server.get("/ping", function (req) {
  return { status: 200, body: "pong" };
});

server.post("/echo", function (req) {
  return {
    status: 200,
    headers: { "content-type": "text/plain; charset=utf-8" },
    body: req.body,
  };
});

server.get(/^\/hello$/, function (req) {
  return req.query.name || "hello";
});

server.get("/assets/*", staticFileHandler("./_sys"));

server.start();
```
