# JS API Reference

This document covers APIs implemented in JavaScript on top of the built-in C host APIs. Their sources live under `shared/flash_data/_sys`, are merged into the selected application's LittleFS image, and are typically loaded from `apps/<app>/flash_data/index.js`.

## Loading

Load these helpers before using them:

```js
load("_sys/display.js");
load("_sys/ui.js");
```

An application entry point may do this from `/littlefs/index.js`; the default `minimal` app intentionally loads nothing.

## `display` Helpers

These helpers are implemented in JavaScript on top of the `i2c` module and live under `/littlefs/_sys/display/`.

- `display.VERSION`
  Current JS display layer version, `"0.4.0"`.
- `display.Surface`
  Base surface contract. It provides metadata and text measurement helpers only; drawing methods must be implemented by the concrete driver.
- `display.registerDriver(name, factory)`
  Register a hardware driver factory.
- `display.create(options)` / `display.open(options)`
  Create or initialize a display by driver name. `options.driver` is required.
- `display.listDrivers()`
  Return the registered driver names.
- `display.mono1(value)`
  Return a packed 1-bit color, `0` or `1`.
- `display.gray4(value)` / `display.gray8(value)`
  Return packed grayscale colors for future gray display drivers.
- `display.rgb565(red, green, blue)`
  Return a packed RGB565 color.

Built-in driver names are `ssd1306`, `st7789`, and `wlk1501spi8p`.

Supported `options` fields:

- `sda`, `scl`, `freqHz`, `timeoutMs`, `internalPullup`
  Passed through to `i2c.open(...)` when the driver needs to configure or reopen its internal `I2CBus` handle.
- `address`
  SSD1306 I2C address, default `0x3c`.
- `host`, `sclk`, `mosi`, `miso`, `cs`, `dc`, `reset`, `backlight`, `freqHz`, `maxTransferSize`, `chunkBytes`, `storage`
  ST7789 SPI/GPIO options.
- `width`, `height`
  Display size, default `128x64`.
- `spacing`
  Extra inter-character spacing for `drawText()`.

The built-in display drivers require the native `displayBuffer` module. Drawing primitives are forwarded to the native buffer, while JavaScript manages fonts, colors, dirty/flush policy, and panel command sequencing. ST7789 can pack immediate-mode UI drawing into one command stream per frame, append it to a retained native command buffer, replay it once into the RGB565 framebuffer, then flush through a retained span source from `createSpanSource(...)` and SPI `writeSource(...)`. SSD1306 uses a native mono buffer and can flush chunks through I2C `writeChunks(...)` when needed.

The display stdlib loads `_sys/display/fonts/mono5x7.eqf` as `display.defaultFont`. Additional fonts can be loaded from LittleFS with `display.loadFont(path, name?)`. The file must use the EQF1 fixed bitmap format documented in the C API. Pass the returned font with `{ font }` to `drawText()` or `measureText()`.

Mapped font sets can be loaded with `display.loadFontSet(path)` or directly with `display.loadMappedFont(path, size, name?)`. The manifest uses the same JSON file as `scripts/font_to_eqf.py --format manifest`, mapping input characters to safe EQF1 ASCII slots before drawing. This supports small Chinese UI strings without changing the native `displayBuffer` text API.

```js
var cjk16 = display.loadMappedFont("_sys/fonts/droid-cjk.json", "16");
screen.drawText(8, 40, "中文显示", {
  color: display.rgb565(255, 255, 255),
  font: cjk16
});
```

Display instance methods use the surface foreground color when an optional
`color` argument is omitted, except `clear()`/`fill()`, which use the background.

- `init()`
  Initialize the panel and clear the framebuffer.
- `clear(color?)` / `fill(color?)`
  Fill the local framebuffer with a packed color. Defaults to the surface background color.
- `setPixel(x, y, color?)` / `getPixel(x, y)`
  Write or read one packed pixel color.
- `fillRect(x, y, width, height, color?)`
  Fill a rectangle in the framebuffer.
- `drawLine(x0, y0, x1, y1, color?)`
  Draw a line with Bresenham logic.
- `drawRect(x, y, width, height, color?)`
  Draw a rectangle outline.
- `drawCircle(x, y, radius, color?)` / `fillCircle(x, y, radius, color?)`
  Draw or fill a circle.
- `drawEllipse(x, y, rx, ry, color?, options?)` / `fillEllipse(x, y, rx, ry, color?)`
  Draw or fill an ellipse. `options.segments` controls outline tessellation.
- `drawRoundRect(x, y, width, height, radius, color?)` / `fillRoundRect(x, y, width, height, radius, color?)`
  Draw or fill a rounded rectangle.
- `drawPolyline(points, color?)` / `drawPolygon(points, color?)` / `fillPolygon(points, color?)`
  Draw or fill point lists. Points can be `[[x, y], ...]`, `[{ x, y }, ...]`, or a flat `[x0, y0, x1, y1, ...]` array.
- `drawTriangle(x0, y0, x1, y1, x2, y2, color?)` / `fillTriangle(x0, y0, x1, y1, x2, y2, color?)`
  Convenience triangle helpers.
- `drawQuadraticBezier(x0, y0, cx, cy, x1, y1, color?, options?)`
  Draw a quadratic Bezier curve. `options.segments` defaults to `24`.
- `drawCubicBezier(x0, y0, c1x, c1y, c2x, c2y, x1, y1, color?, options?)`
  Draw a cubic Bezier curve. `options.segments` defaults to `32`.
- `drawBitmap(x, y, bitmap, options?)`
  Draw a bitmap shaped as `{ width, height, pixels }` with `{ color, background }`.
- `drawChar(x, y, ch, options?)`
  Draw one glyph using the default EQF font.
- `drawText(x, y, text, options?)`
  Draw text with `{ color, spacing, font, background }`. Text background is transparent by default; set `background` to explicitly fill each glyph cell, or `null` to keep it transparent.
- `measureText(text, style?)`
  Return `{ width, height, lines }` for the selected font.
- `flush()`
  Write the framebuffer to the panel over I2C.
- `flushRect(x, y, width, height)`
  Write one framebuffer rectangle to drivers that support partial refresh, such as ST7789.
- `flushRects(rects, options?)`
  Write multiple rectangles on drivers that support partial refresh. ST7789 accepts `{ x, y, width, height }`, `{ x, y, w, h }`, or `[x, y, width, height]` entries, and may merge high-coverage regions into one transfer to reduce panel window-command overhead. Pass `{ merge: false }` to force separate transfers.
- `on()` / `off()`
  Turn the panel on or off.
- `invert(enabled)`
  Toggle inverse display mode.
- `contrast(value)`
  Set contrast `0..255`.
- `close()`
  Release the native framebuffer and owned I2C/SPI handles, mark the surface
  not ready, and return `true`. Call this before discarding a display that may
  be replaced or reopened at runtime.

Example:

```js
var oled = display.open({ driver: "ssd1306", sda: 5, scl: 6, address: 0x3c });
oled.clear();
oled.drawText(0, 0, "HELLO", { color: display.mono1(1) });
oled.drawRect(0, 10, 64, 18, display.mono1(1));
oled.flush();
```

## `ui` Helpers

These helpers are implemented in JavaScript on top of the `display` surface interface and live under `/littlefs/_sys/ui/`.

- `ui.VERSION`
  Current JS UI layer version, `"0.3.1"`.
- `ui.begin(surface, options?)`
  Start an immediate-mode frame on a display surface. Drivers with `beginBatch()` support, such as ST7789, use native command-buffer replay by default; pass `{ batch: false }` to force direct drawing.
- `ui.endFrame()`
  Finish the frame, validate that all layout scopes are closed, and flush the dirty region.
- `ui.frame(surface, render, options?)`
  Convenience wrapper around `begin(...)`, a render callback, and `endFrame()`.
- `ui.input(input?)`
  Set or read the pending normalized input snapshot used by the next frame.
- `ui.row(render?, options?)` / `ui.column(render?, options?)` / `ui.group(render?, options?)` / `ui.panel(render?, options?)`
  Open a layout scope. When `render` is provided, the layout is closed automatically after the callback returns. `panel(...)` also draws a decorated container. For compatibility, the older `ui.row(options, render)` form is still accepted.
- `ui.end()`
  Close the current layout scope.
- `ui.spacer(size | options)` / `ui.separator(options?)`
  Add fixed spacing or a one-pixel separator.
- `ui.text(value, options?)` / `ui.value(label, value, options?)` / `ui.badge(text, options?)`
  Draw simple non-interactive content.
- `ui.statusBar(options?)`
  Draw a compact left/title/right status row.
- `ui.progress(id, value, options?)` / `ui.gauge(id, value, options?)`
  Draw bounded numeric indicators and return the allocated rect.
- `ui.button(id, label, options?)` / `ui.iconButton(id, icon, options?)`
  Draw a focusable command and return `true` when activated.
- `ui.toggle(id, label, value, options?)` / `ui.checkbox(id, label, value, options?)`
  Draw a focusable boolean control and return the updated value.
- `ui.slider(id, value, options?)` / `ui.stepper(id, value, options?)`
  Draw a focusable numeric control and return the updated value.
- `ui.list(id, items, selectedIndex, options?)` / `ui.menu(id, items, selectedIndex, options?)`
  Draw a compact selectable list and return the updated selected index.
- `ui.tabs(id, tabs, selectedIndex, options?)`
  Draw a tab strip and return the updated selected index.
- `ui.softkeys(left, center, right, options?)`
  Draw a three-zone softkey bar and return `"left"`, `"center"`, `"right"`, or `null`.

Frame options:

- `x`, `y`, `width`, `height`
  Root frame bounds. Defaults to the whole surface.
- `clear`
  Clear the root frame before drawing. Defaults to `true` for the first frame on a surface and `false` afterwards.
- `clearColor`
  Packed display color used when clearing.
- `flush`
  Set `false` to draw without flushing.
- `partial`
  Set `false` to force full `surface.flush()`. By default `endFrame()` uses `flushRects(...)` or `flushRect(...)` when available.
- `gcBeforeFlush`
  Set `false` to skip the automatic `gc()` before first-frame or large dirty-region flushes.
- `focusVisible`
  Controls whether the focused control draws its outline. By default the runtime keeps a logical focus target for button/encoder input but only shows the outline after keyboard, encoder, or touch input.
- `input`
  Normalized input snapshot for this frame.
- `theme`
  Packed display colors overriding the default mono or RGB565 theme.

Normalized input fields:

- `up`, `down`
  Move focus between focusable controls.
- `left`, `right`
  Adjust focused value controls such as sliders, steppers, lists, and tabs.
- `ok`
  Activate the focused button/toggle/checkbox or softkey center.
- `back`
  Activate the softkey left action.
- `encoderDelta`
  Adjust focused value controls.
- `touch`
  Optional `{ x, y, pressed }` touch point. A press inside a focusable control focuses and activates it.

Common control/layout options:

- `x`, `y`, `width`, `height`
  Pixel bounds or fixed outer size.
- `padding`
  Number or `{ top, right, bottom, left }`; `{ x, y }` sets horizontal and vertical padding.
- `gap`
  Space between children in a row or column layout.
- `align`
  Cross-axis alignment: `"start"`, `"center"`, `"end"`, or `"stretch"`.
- `background`
  Packed fill color.
- `border`
  Draw a 1-pixel border.
- `borderColor`
  Packed border color.
- `radius`, `borderRadius`
  Rounded-corner radius in pixels when the display surface supports round rectangles.
- `outline`
  Focus-visible controls draw a 2-pixel outline by default. Set to `false` to disable it for a specific control.
- `outlineColor`, `outlineWidth`
  Packed focus outline color and pixel width. Defaults to the theme focus color and `2`.
- `color`
  Packed text/control foreground color.
- `font`, `spacing`
  Text style forwarded to `surface.drawText(...)` and `surface.measureText(...)`.

Common declaration/config options:

- `id`
  Optional stable ID for non-interactive/decorative controls that need persistent dirty tracking. Interactive controls use the explicit `id` argument.
- `min`, `max`, `step`
  Numeric bounds for progress, gauges, sliders, and steppers.
- `visibleCount`, `rowHeight`
  List/menu sizing controls.
- `left`, `title`, `center`, `right`
  Status-bar labels.

Example:

```js
var screen = display.open({ driver: "st7789" });
var wifiEnabled = true;
var brightness = 40;
var selectedTab = 0;

ui.begin(screen, { clear: true, partial: true });
ui.column(function () {
  ui.statusBar({ left: "ESP32", title: "Control", right: "74%" });
  selectedTab = ui.tabs("mode", ["Main", "Net", "Info"], selectedTab);

  ui.panel(function () {
    ui.text("Immediate UI");
    wifiEnabled = ui.toggle("wifi", "WiFi", wifiEnabled);
    brightness = ui.slider("brightness", brightness, { min: 0, max: 100, step: 5 });
  }, { height: 78, padding: 4, gap: 4 });

  if (ui.button("apply", "Apply", { width: 48 })) {
    print("apply");
  }
}, { padding: 6, gap: 4 });
ui.endFrame();
```

Optional UI helpers are loaded separately when needed:

- `load("_sys/ui/control.js")`
  Adds `ui.control`, a thin command/input helper for REPL, programmatic control, and optional GPIO buttons. It does not change the core UI input model; call `ui.control.read()` and pass the returned snapshot to `ui.begin(..., { input })`.
- `load("_sys/ui/fps.js")`
  Adds `ui.fps(id, options?)`, a small frame-rate meter implemented by composing core UI primitives. It keeps sampling state by `id`, returns the sampled FPS value, and accepts `enabled: false` to clear its reserved area without showing text. `sampleMs` controls the sample window and `precision` controls decimal places.

Example:

```js
load("_sys/ui/control.js");
load("_sys/ui/fps.js");

var showFps = true;

ui.begin(screen, {
  clear: true,
  partial: true,
  input: ui.control.read()
});
ui.column(function () {
  showFps = ui.toggle("showFps", "FPS", showFps);
  ui.control.indicator({ label: "IN", width: 74 });
  ui.fps("fps", { enabled: showFps, sampleMs: 1000, precision: 1 });
}, { padding: 6, gap: 4 });
ui.endFrame();

// From the REPL or another script:
ui.control.press("down");
ui.control.press("ok");
ui.control.encoder(1);
```

GPIO buttons can be composed directly through the same helper:

```js
ui.control.bindButtons({
  up: 1,
  down: 2,
  ok: { pin: 3, activeLow: true }
});
```
