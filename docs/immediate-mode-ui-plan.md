# Immediate Mode UI Plan

This document is a working plan for replacing the current JavaScript UI helper
under [`flash_data/_sys/ui`](/home/zach/esp32qjs/flash_data/_sys/ui) with an
immediate-mode, declarative UI layer for small embedded displays.

The goal is not to add a native UI ABI. The UI layer should remain a versioned
LittleFS JavaScript library built on top of the existing `display` surface and
`displayBuffer` APIs.

## Starting Point

Before this rewrite, the UI implementation was a small retained tree renderer:

- `ui.box(...)`, `ui.row(...)`, `ui.column(...)`, `ui.text(...)`,
  `ui.spacer(...)`, and `ui.padding(...)` allocate node objects.
- `ui.layout(...)` mutates each node with `frame` and `contentFrame`.
- `ui.render(...)` clears the whole surface by default, lays out the whole tree,
  paints the whole tree, and calls `surface.flush()`.
- There is no persistent control state, input routing, focus model, dirty-rect
  tracking, or widget set beyond layout/text primitives.

The public shape was documented in
[`docs/js-api.md`](/home/zach/esp32qjs/docs/js-api.md) and typed in
[`types/esp32qjs-js-api.d.ts`](/home/zach/esp32qjs/types/esp32qjs-js-api.d.ts).
Those files need to stay updated together with the implementation.

## Goals

- Make each frame a direct UI declaration instead of building and retaining a
  reusable node tree.
- Keep the API small enough for scripts running on ESP32-class devices.
- Support both monochrome OLED-style displays and RGB565 SPI TFT displays.
- Make directional buttons, rotary encoders, and optional touch input natural.
- Provide common controls that are useful without a browser-like layout engine.
- Allow partial redraws when the display driver supports rect flushing, while
  keeping full-screen rendering as the simple fallback.
- Keep widget behavior in JavaScript. Native code should stay focused on
  buffers, drawing primitives, byte sources, and peripheral transport.

## Non-Goals

- Do not move panel drivers, widget policy, animation policy, or input policy
  into C.
- Do not add SPI-specific UI shortcuts.
- Do not implement a CSS-like style engine.
- Do not optimize native clipping, multi-rect merging, or blitting until a real
  UI workload shows that the current path is the bottleneck.
- Do not preserve the existing retained-node API unless compatibility is still
  valuable when implementation starts.

## Proposed API Shape

The core API should be frame-scoped:

```js
load("_sys/display.js");
load("_sys/ui.js");

var screen = display.open({ driver: "st7789" });
var wifiEnabled = false;
var batteryPercent = 74;
var inputSnapshot = readBoardInput();

ui.begin(screen, {
  clear: true,
  input: inputSnapshot,
  theme: ui.theme.dark
});

ui.column().style({ gap: 4, padding: 4 });
ui.text("Status");
wifiEnabled = ui.toggle("wifi.enabled", "WiFi", wifiEnabled).value;
ui.progress("battery", batteryPercent, { min: 0, max: 100 });
ui.button("apply", "Apply");
ui.end();

ui.endFrame();
```

A callback helper can be added for readability:

```js
ui.frame(screen, function () {
  ui.column().style({ gap: 4, padding: 4 });
  ui.text("Status");
  ui.button("apply", "Apply");
  ui.end();
});
```

The low-allocation stack API should be the primary implementation path. Callback
helpers should be sugar over the same frame stack, not a separate tree builder.

## Frame Context

`ui.begin(surface, options)` should create or reuse a frame context containing:

- the active surface
- root frame bounds
- current layout stack
- active theme
- current input snapshot
- focus state
- per-control persistent state, keyed by explicit IDs
- dirty region accumulator

`ui.endFrame()` should:

- finalize any open layout scopes with clear errors for mismatched `ui.end()`
- flush the dirty region when supported
- fall back to `surface.flush()` when partial flushing is not available
- retain only the minimal state needed for the next frame

## IDs and State

Interactive controls should require stable IDs:

```js
var selected = ui.toggle("settings.wifi", "WiFi", selected).value;
var level = ui.slider("settings.brightness", level, { min: 0, max: 100 }).value;
```

The UI runtime should store only generic interaction state by ID:

- focus
- pressed/held status
- edit mode
- last frame rect
- previous value snapshot when needed for invalidation

Application data should stay in application variables. Value controls expose
updated values through their component `.value` instead of owning business state.

## Layout Primitives

Initial layout primitives:

- `ui.row(options?)`
- `ui.column(options?)`
- `ui.panel(options?)`
- `ui.group(options?)`
- `ui.end()`
- `ui.spacer(sizeOrOptions)`
- `ui.separator(options?)`

Supported layout options should stay pixel-oriented:

- `x`, `y`, `width`, `height`
- `padding`
- `gap`
- `align`
- `justify`
- `flex`
- `minWidth`, `minHeight`

Avoid percentage sizing, wrapping text layout, and implicit global scaling in the
first version.

## Display Primitives

Initial non-interactive primitives:

- `ui.text(value, options?)`
- `ui.value(label, value, options?)`
- `ui.badge(text, options?)`
- `ui.icon(name, options?)` if a tiny built-in icon set is added
- `ui.progress(id, value, options?)`
- `ui.gauge(id, value, options?)`
- `ui.statusBar(options?)`

Optional composition helpers should live in separate files and be loaded on
demand, for example `_sys/ui/control.js` adding `ui.control` command/input
helpers, or `_sys/ui/fps.js` adding `ui.fps(id, options?)` on top of core
primitives.

Text options should expose the display layer's existing font support:

- `font`
- `color`
- `background`
- `spacing`
- `align`

## Embedded Controls

Initial interactive controls:

- `ui.button(id, label, options?)`
- `ui.iconButton(id, icon, options?)`
- `ui.toggle(id, label, value, options?)`
- `ui.checkbox(id, label, value, options?)`
- `ui.slider(id, value, options?)`
- `ui.stepper(id, value, options?)`
- `ui.list(id, items, selectedIndex, options?)`
- `ui.menu(id, items, selectedIndex, options?)`
- `ui.tabs(id, tabs, selectedIndex, options?)`
- `ui.softkeys(left, center, right, options?)`

Chained component values should stay easy to read:

```js
if (ui.button("save", "Save").value) {
  saveSettings();
}

brightness = ui.slider("brightness", brightness, { min: 0, max: 100, step: 5 }).value;
mode = ui.tabs("mode", ["Auto", "Manual"], mode).value;
```

## Input Model

The UI layer should accept a normalized input snapshot instead of reading GPIOs
directly inside controls.

Recommended event model:

```js
ui.input({
  up: false,
  down: true,
  left: false,
  right: false,
  ok: false,
  back: false,
  encoderDelta: 0,
  touch: null
});
```

This keeps hardware policy outside the UI core. Board scripts can adapt GPIO
buttons, rotary encoders, capacitive touch, or external input modules into this
shape.

## Rendering and Dirty Regions

The first immediate-mode version should support two render paths:

- full render: clear the root bounds and flush the whole surface
- partial render: invalidate changed control rects and flush the bounding dirty
  region when the surface supports it

Dirty tracking should be per control ID:

- current rect
- previous rect
- value/style changes that require repaint
- focus/pressed state changes

This is enough for useful embedded screens without introducing complex native
clipping or multi-rect optimization up front.

## Theme Model

Themes should be plain JavaScript objects with packed display colors:

```js
ui.theme.dark = {
  background: display.rgb565(0, 0, 0),
  foreground: display.rgb565(255, 255, 255),
  accent: display.rgb565(0, 180, 255),
  danger: display.rgb565(255, 60, 60),
  border: display.rgb565(80, 80, 80)
};
```

The default theme should adapt to `surface.pixelFormat` so monochrome surfaces
work without RGB-only assumptions.

## Migration Plan

1. Add the immediate-mode core behind `_sys/ui/core.js`.
2. Replace retained `UINode` types with frame/context/control types.
3. Update `docs/js-api.md` and `types/esp32qjs-js-api.d.ts`.
4. Add a small example under `flash_data/demo/` that exercises layout, focus,
   input, and partial refresh.
5. Keep the old API only if there is an active script that still needs it.
   Otherwise, remove it cleanly and document the new API as the only supported
   shape.

## Validation Plan

Low-cost checks:

- `node --check flash_data/_sys/ui/core.js`
- `node --check flash_data/demo/<ui-demo>.js`
- `python scripts/remote.py build-fs`

Board-backed checks:

- `python scripts/remote.py --board xiao_esp32s3 flash-fs`
- `python scripts/remote.py --board xiao_esp32s3 monitor`

Manual checks should verify:

- frame-to-frame controls keep stable state by ID
- focus moves correctly with directional input
- button/toggle/slider `.value` results are correct
- monochrome and RGB565 surfaces both render legibly
- partial mode updates dynamic text and focus highlights without freezing stale
  UI regions

## Initial Implementation Decisions

- The first implementation is a clean break from retained `UINode` rendering.
  `ui.render(...)`, `ui.layout(...)`, and `ui.box(...)` are removed from the
  documented API.
- Both explicit frame calls and callback-style `ui.frame(...)` are public.
- The first icon path is text/glyph based. A bundled bitmap icon set can be
  added later if concrete screens need it.
