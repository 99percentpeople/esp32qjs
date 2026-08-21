# JS API Reference

This document covers APIs implemented in JavaScript on top of the built-in C host APIs. Their sources live under `shared/flash_data/_sys`, are merged into the selected application's LittleFS image, and are typically loaded from `apps/<app>/flash_data/index.js`.

## Loading

Load the entry point for the panel in use, then load any application-owned UI:

```js
framework.load("display/wlk1501spi8p.js");
load("ui.js");
```

`ui.js` in this example belongs to the selected app. The framework does not
ship `_sys/ui`; layout, controls, and rendering policy are application code.

`_sys/display.js` loads only the display facade, surface, fonts, and registries.
Use `_sys/display/ssd1306.js`, `_sys/display/st7789.js`, or
`_sys/display/wlk1501spi8p.js` to load one hardware stack, and
`_sys/display/all.js` only when every built-in is needed. An application entry
point may do this from `/littlefs/index.js`; the default `minimal` app
intentionally loads nothing.

## `display` Helpers

The display library is composed from three independent layers:

```text
Display facade -> Surface -> bitmap
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
- `drawMask`, `blit`, `drawChar`, `drawText`, `measureText`
  `blit(source, options?)` forwards to the native `Bitmap` transform worker. A
  raw `CameraFrame`, Bitmap, or raw descriptor is accepted without a JavaScript
  pixel loop; callers can crop, rotate, flip, resize, normalize gray output,
  and request `dither: "bayer4x4"` for `mono1` output.
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
