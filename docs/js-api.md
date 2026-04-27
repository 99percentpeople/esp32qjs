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

The built-in display drivers require the native `displayBuffer` module. Drawing primitives are forwarded to the native buffer, while JavaScript manages fonts, colors, dirty/flush policy, and panel command sequencing. ST7789 uses a retained RGB565 span source from `createSpanSource(...)` and flushes it through SPI `writeSource(...)`. SSD1306 uses a native mono buffer and can flush chunks through I2C `writeChunks(...)` when needed.

The display stdlib loads `_sys/display/fonts/mono5x7.eqf` as `display.defaultFont`. Additional fonts can be loaded from LittleFS with `display.loadFont(path, name?)`. The file must use the EQF1 fixed bitmap format documented in the C API. Pass the returned font with `{ font }` to `drawText()` or `measureText()`.

Mapped font sets can be loaded with `display.loadFontSet(path)` or directly with `display.loadMappedFont(path, size, name?)`. The manifest uses the same JSON file as `scripts/font_to_eqf.py --format manifest`, mapping input characters to safe EQF1 ASCII slots before drawing. This supports small Chinese UI strings without changing the native `displayBuffer` text API.

```js
var cjk16 = display.loadMappedFont("_sys/display/fonts/droid-cjk.json", "16");
screen.drawText(8, 40, "中文显示", {
  color: display.rgb565(255, 255, 255),
  font: cjk16
});
```

Display instance methods:

- `init()`
  Initialize the panel and clear the framebuffer.
- `clear(color?)` / `fill(color?)`
  Fill the local framebuffer with a packed color. Defaults to the surface background color.
- `setPixel(x, y, color)` / `getPixel(x, y)`
  Write or read one packed pixel color.
- `fillRect(x, y, width, height, color)`
  Fill a rectangle in the framebuffer.
- `drawLine(x0, y0, x1, y1, color)`
  Draw a line with Bresenham logic.
- `drawRect(x, y, width, height, color)`
  Draw a rectangle outline.
- `drawCircle(x, y, radius, color)` / `fillCircle(x, y, radius, color)`
  Draw or fill a circle.
- `drawEllipse(x, y, rx, ry, color, options?)` / `fillEllipse(x, y, rx, ry, color)`
  Draw or fill an ellipse. `options.segments` controls outline tessellation.
- `drawRoundRect(x, y, width, height, radius, color)` / `fillRoundRect(x, y, width, height, radius, color)`
  Draw or fill a rounded rectangle.
- `drawPolyline(points, color)` / `drawPolygon(points, color)` / `fillPolygon(points, color)`
  Draw or fill point lists. Points can be `[[x, y], ...]`, `[{ x, y }, ...]`, or a flat `[x0, y0, x1, y1, ...]` array.
- `drawTriangle(x0, y0, x1, y1, x2, y2, color)` / `fillTriangle(x0, y0, x1, y1, x2, y2, color)`
  Convenience triangle helpers.
- `drawQuadraticBezier(x0, y0, cx, cy, x1, y1, color, options?)`
  Draw a quadratic Bezier curve. `options.segments` defaults to `24`.
- `drawCubicBezier(x0, y0, c1x, c1y, c2x, c2y, x1, y1, color, options?)`
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
  Fill the node frame before painting children with a packed display color.
- `border`
  Draw a 1-pixel border around the node frame.
- `borderColor`
  Packed border color. Defaults to the surface foreground color.
- `color`
  Packed text color for `ui.text(...)`, for example `display.mono1(1)` or `display.rgb565(255, 255, 255)`.

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
