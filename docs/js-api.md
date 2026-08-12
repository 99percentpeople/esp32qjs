# JS API Reference

This document covers APIs implemented in JavaScript on top of the built-in C host APIs. Their sources live under `shared/flash_data/_sys`, are merged into the selected application's LittleFS image, and are typically loaded from `apps/<app>/flash_data/index.js`.

## Loading

Load the entry point for the panel in use, then load optional UI helpers:

```js
framework.load("display/wlk1501spi8p.js");
framework.load("ui.js");
```

`_sys/display.js` loads only the display facade, surface, fonts, and registries.
Use `_sys/display/ssd1306.js`, `_sys/display/st7789.js`, or
`_sys/display/wlk1501spi8p.js` to load one hardware stack, and
`_sys/display/all.js` only when every built-in is needed. An application entry
point may do this from `/littlefs/index.js`; the default `minimal` app
intentionally loads nothing.

## `display` Helpers

The display library is composed from three independent layers:

```text
Display facade -> Surface -> displayBuffer
              -> PanelDriver -> DisplayTransport -> SPI/I2C/GPIO
```

- `display.VERSION`
  Current JS display-layer version, `"0.5.0"`.
- `display.Display`
  Public lifecycle and drawing facade used by applications and `ui`.
- `display.Surface`
  Hardware-independent renderer that owns one native framebuffer.
- `display.transports`
  Registry for byte transports. Built-ins are `i2c` and `spi4wire`.
- `display.drivers`
  Registry for panel-controller drivers. Built-ins are `ssd1306` and `st7789`.
- `display.profiles`
  Registry for complete hardware presets. `wlk1501spi8p` is a profile, not a
  panel driver.
- `display.create(driver, options?)`
  Create a `Display` without opening panel hardware. Drawing is allowed before
  `open()`.
- `display.open(driver, options?)`
  Create and open a `Display`, then present its initial framebuffer.

The old flat `display.open({ driver: "...", ... })` API has been removed. Bus,
device, panel, surface, and display settings are grouped by their owning layer.
Unknown options fail before hardware initialization.

### Explicit ST7789 Construction

```js
framework.load("display/st7789.js");

var transport = display.transports.create("spi4wire", {
  busOptions: {
    host: spi.DEFAULT_HOST,
    sclk: spi.DEFAULT_SCLK,
    mosi: spi.DEFAULT_MOSI,
    miso: -1,
    maxTransferSize: 16384
  },
  deviceOptions: {
    cs: spi.DEFAULT_CS,
    mode: 0,
    freqHz: 40000000,
    queueSize: 2
  },
  pins: { dc: 4, reset: 5, backlight: 6 }
});

var driver = display.drivers.create("st7789", {
  transport: transport,
  width: 240,
  height: 240,
  rotation: 0
});

var screen = display.open(driver, {
  surface: {
    storage: "dma",
    fallbackStorage: "auto",
    chunkBytes: 16384
  },
  metrics: true
});

screen.clear();
screen.drawText(8, 8, "HELLO");
screen.present();
```

`SPI4Wire` transport options are separated into `busOptions`, `deviceOptions`,
and `pins`. If no bus or device is supplied, the transport creates and owns
both. A supplied bus is borrowed; the transport opens and owns only its display
device. A supplied device is also borrowed. Closing the display never closes a
borrowed handle.

### Explicit SSD1306 Construction

```js
load("_sys/display/ssd1306.js");

var transport = display.transports.create("i2c", {
  busOptions: {
    sda: 5,
    scl: 6,
    freqHz: 400000,
    timeoutMs: 1000,
    internalPullup: true
  },
  address: 0x3c
});
var driver = display.drivers.create("ssd1306", {
  transport: transport,
  width: 128,
  height: 64
});
var oled = display.open(driver);

oled.clear();
oled.drawText(0, 0, "HELLO", { color: display.mono1(1) });
oled.drawRect(0, 10, 64, 18, display.mono1(1));
oled.present();
```

### WLK1501SPI8P Profile

```js
load("_sys/display/wlk1501spi8p.js");

var screen = display.profiles.open("wlk1501spi8p", {
  transport: {
    deviceOptions: { freqHz: 80000000 }
  },
  surface: {
    chunkBytes: 32768
  },
  display: {
    metrics: true
  }
});
```

The profile creates the normal `SPI4Wire` transport, ST7789 driver, Surface,
and Display objects. `screen.driver.name` remains `"st7789"`, while
`screen.profileName` is `"wlk1501spi8p"`.

### Display Drawing and Presentation

Drawing methods operate only on the local `Surface`; they do not touch panel
hardware. Optional colors use the surface foreground, except `clear()` and
`fill()`, which use its background.

- `clear`, `fill`, `setPixel`, `getPixel`, `fillRect`
- `drawLine`, `drawRect`, `drawCircle`, `fillCircle`
- `drawEllipse`, `fillEllipse`, `drawRoundRect`, `fillRoundRect`
- `drawPolyline`, `drawPolygon`, `fillPolygon`
- `drawTriangle`, `fillTriangle`
- `drawQuadraticBezier`, `drawCubicBezier`
- `drawBitmap`, `drawChar`, `drawText`, `measureText`
- `beginBatch()` / `endBatch(batch)`
  Record/replay native drawing commands when `screen.supports("batch")`.

Presentation methods are:

- `present(regions?, options?)`
  Present current dirty bounds, one rectangle, or a rectangle list. Successful
  presentation clears native dirty state; failed presentation preserves it for
  retry. `{ merge: false }` keeps multiple regions separate.
- `flush()`
  Force a full-screen presentation.
- `flushRect(...)` / `flushRects(...)`
  Explicit region convenience methods.

Panel controls use a consistent capability-checked API:

- `supports(name)`
- `setPower(enabled)`
- `setInverted(enabled)`
- `setContrast(value)`
- `setBacklight(enabled)`

Capability names are `partialPresent`, `multiRegion`, `power`, `inversion`,
`contrast`, `backlight`, `batch`, and `directSource`.

`stats()` returns common presentation counters including `presents`, `regions`,
`pixels`, `bytes`, `chunks`, `directTransfers`, `totalUs`, `prepareUs`,
`panelUs`, and `transferUs`. Timing collection is enabled with
`{ metrics: true }`. `resetStats()` resets facade, driver, and transport
statistics.

`close()` is idempotent and terminal. It turns off a managed backlight, closes
the driver/transport, releases command/frame sources, and closes the native
framebuffer. Drawing or reopening after close throws.

### Fonts

The display core loads `_sys/display/fonts/mono5x7.eqf` as
`display.defaultFont`. Load additional EQF1 fonts with
`display.loadFont(path, name?)`, `display.loadFontSet(path)`, or
`display.loadMappedFont(path, size, name?)`.

```js
var cjk16 = display.loadMappedFont("_sys/fonts/droid-cjk.json", "16");
screen.drawText(8, 40, "中文显示", {
  color: display.rgb565(255, 255, 255),
  font: cjk16
});
```

## `ui` Helpers

These helpers are implemented in JavaScript on top of the layered `display.Display` facade and live under `/littlefs/_sys/ui/`.

- `ui.VERSION`
  Current JS UI layer version, `"0.3.1"`.
- `ui.begin(screen, options?)`
  Start an immediate-mode frame on an open `display.Display`. Displays advertising the `batch` capability use native command-buffer replay by default; pass `{ batch: false }` to force direct drawing.
- `ui.endFrame()`
  Finish the frame, validate that all layout scopes are closed, and flush the dirty region.
- `ui.frame(screen, render, options?)`
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
  Set `false` to force full `screen.flush()`. By default `endFrame()` calls `screen.present(...)` with tracked dirty regions when `partialPresent` is supported.
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
var screen = display.profiles.open("wlk1501spi8p");
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
