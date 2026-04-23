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
  Current JS display layer version, `"0.2.0"`.
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
  Passed through to `i2c.open(...)` when the driver needs to configure or reopen its internal `I2CBus` handle.
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
