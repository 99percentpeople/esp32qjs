# JS API Reference

This document covers APIs implemented in JavaScript on top of the built-in C host APIs. These helpers live on LittleFS and are typically loaded from `index.js`.

## Loading

Load these helpers before using them:

```js
load("_sys/display.js");
load("_sys/ui.js");
```

The default startup script commonly does this from `/littlefs/index.js`.

## `display` Helpers

These helpers are implemented in JavaScript on top of the `i2c` module and live under `/littlefs/_sys/display/`.

- `display.VERSION`
  Current JS display layer version, `"0.3.0"`.
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

Built-in driver names are `ssd1306`, `st7789`, and `wlk1501spi8p`.

Supported `options` fields:

- `sda`, `scl`, `freqHz`, `timeoutMs`, `internalPullup`
  Passed through to `i2c.open(...)` when the driver needs to configure or reopen its internal `I2CBus` handle.
- `address`
  SSD1306 I2C address, default `0x3c`.
- `host`, `sclk`, `mosi`, `miso`, `cs`, `dc`, `reset`, `backlight`, `freqHz`, `maxTransferSize`, `chunkBytes`
  ST7789 SPI/GPIO options.
- `width`, `height`
  Display size, default `128x64`.
- `spacing`
  Extra inter-character spacing for `drawText()`.

When the firmware exposes `esp32.info().features.displayBuffer`, the ST7789 driver uses a native RGB565 `displayBuffer` internally and flushes native byte views through `spi.write(...)`. If the feature is disabled, it falls back to the pure JavaScript mono surface path.

The display stdlib loads `_sys/display/fonts/mono5x7.eqf` as `display.defaultFont`. Additional fonts can be loaded from LittleFS with `display.loadFont(path, name?)`. The file must use the EQF1 fixed bitmap format documented in the C API. Pass the returned font with `{ font }` to `drawText()` or `measureText()`.

Mapped font sets can be loaded with `display.loadFontSet(path)` or directly with `display.loadMappedFont(path, size, name?)`. The manifest uses the same JSON file as `scripts/font_to_eqf.py --format manifest`, mapping input characters to safe EQF1 ASCII slots before drawing. This supports small Chinese UI strings without changing the native `displayBuffer` text API.

```js
var cjk16 = display.loadMappedFont("_sys/display/fonts/droid-cjk.json", "16");
screen.drawText(8, 40, "中文显示", 0xffff, { font: cjk16 });
```

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
  Draw one glyph using the default EQF font.
- `drawText(x, y, text, enabled, spacingOrStyle?)`
  Draw text. Pass a number for spacing, or `{ spacing, font }` for a loaded EQF font.
- `measureText(text, style?)`
  Return `{ width, height, lines }` for the selected font.
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

These helpers are implemented in JavaScript on top of the `display` surface interface and live under `/littlefs/_sys/ui/`.

- `ui.VERSION`
  Current JS UI layer version, `"0.1.0"`.
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
  Text color for `ui.text(...)`. Use `true`/`false` for the active surface foreground/background, or an RGB565 number on color surfaces.

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
