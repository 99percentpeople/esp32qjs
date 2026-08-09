# Display and Panel Driver Redesign

Status: implemented as JS display-layer version `0.5.0`.

## Decisions

This design uses the following decisions:

- `Display` remains the public drawing facade. Application and UI code calls
  `screen.drawText(...)`, `screen.present()`, and related methods directly.
- `Display`, panel drivers, and transports are three separate object layers.
- The new API is intentionally breaking. The flat
  `display.open({ driver: "...", ... })` form will not be retained as a
  compatibility API.
- Controller drivers such as ST7789 and SSD1306 do not inherit from `Surface`
  and do not implement drawing primitives.
- Board/module presets such as `wlk1501spi8p` are profiles, not controller
  drivers.

This is a JavaScript-layer redesign and does not require a Host API version
bump. It does require coordinated changes to the JS API version, examples,
applications, and declarations.

## Why the Previous Model Changed

Before `0.5.0`, each hardware driver was a `display.Surface` subclass. That put
too many responsibilities in one object:

- allocating and drawing into a native `displayBuffer`;
- normalizing colors, fonts, and text;
- recording command batches;
- opening SPI/I2C buses and devices;
- configuring GPIO control pins;
- sending controller initialization commands;
- choosing dirty-region and flush behavior;
- collecting rendering and transport performance statistics;
- releasing every resource above.

The result is visible in the current drivers:

- SSD1306 repeats nearly every drawing method only to forward it to its native
  buffer.
- ST7789 combines rendering, command batching, panel sequencing, SPI transfer,
  dirty-region merging, performance accounting, and resource ownership.
- The flat options object mixes unrelated meanings. For example, `freqHz` is
  an I2C bus option for SSD1306 but an SPI device option for ST7789.
- A display always opens and owns its bus, so another device cannot safely
  share an already-open SPI host with it.
- `wlk1501spi8p` is registered as if it were a controller driver even though it
  is an ST7789 wiring and sizing preset.
- Capabilities are discovered by probing optional methods such as
  `flushRect()` and `beginBatch()` instead of using an explicit contract.
- Constructors allocate native buffers while `init()` opens hardware, making
  partial initialization and rollback difficult to reason about.

## Goals

1. Keep application drawing ergonomic through a `Display` facade.
2. Make controller drivers small and independent from rendering primitives.
3. Isolate SPI/I2C/GPIO mechanics and native-handle ownership in transports.
4. Allow displays and other devices to share a caller-owned SPI or I2C bus.
5. Keep the native `displayBuffer`, command-buffer, and span-source fast paths.
6. Make lifecycle states, capabilities, errors, and cleanup deterministic.
7. Allow driver-specific loading so unused drivers do not consume runtime RAM.
8. Give third-party drivers a documented JavaScript extension contract.
9. Keep all shipped JavaScript compatible with the repository's MQuickJS
   syntax gate.

## Non-goals

- A native C display-driver ABI.
- Automatic panel or board detection.
- A retained scene graph or widget toolkit replacement.
- Concurrent or asynchronous panel transfers in the first implementation.
- Sharing one transport object between drivers. Callers share its underlying
  bus handle by creating separate transport objects instead.
- Hot-swapping the driver of an existing `Display`. Close it and create a new
  one.
- Supporting untrusted JavaScript as a sandbox.

## Object Model

```text
Application / ui
       |
       v
+---------------------------+
| Display                   |  public facade and lifecycle owner
| draw*, present, controls  |
+-------------+-------------+
              |
       +------+------+
       |             |
       v             v
+-------------+  +------------------+
| Surface     |  | PanelDriver      |
| rendering   |  | controller logic |
| dirty state |  | region transfer  |
+------+------+  +---------+--------+
       |                   |
       v                   v
 displayBuffer      DisplayTransport
                     SPI4Wire / I2C
                           |
                           v
                   spi / i2c / gpio
```

Composition replaces driver inheritance:

```text
old: SSD1306Display extends Surface and owns everything
new: Display owns Surface + SSD1306Driver; SSD1306Driver owns Transport
```

### Responsibility Matrix

| Layer | Owns | Must not own |
| --- | --- | --- |
| `Display` | lifecycle, public drawing facade, present policy, capabilities, aggregate metrics | panel command bytes, raw bus setup |
| `Surface` | native framebuffer, common drawing, fonts/colors, dirty state, command batching | SPI/I2C/GPIO handles, panel initialization |
| `PanelDriver` | controller geometry, initialization sequence, window commands, power/inversion/contrast semantics | drawing primitives, fonts, framebuffer allocation |
| `DisplayTransport` | SPI/I2C framing, device/bus handles, DC/reset/backlight GPIO, transfer statistics | controller-specific initialization, drawing, dirty-region policy |
| `Profile` | known wiring and nested defaults for one physical module/board combination | new controller behavior |

## Public Construction API

### ST7789

The canonical advanced form constructs all three layers explicitly:

```js
load("_sys/display/st7789.js");

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
  pins: {
    dc: 4,
    reset: 5,
    backlight: 6
  },
  backlightActive: true
});

var driver = display.drivers.create("st7789", {
  transport: transport,
  width: 240,
  height: 240,
  rotation: 0,
  columnOffset: 0,
  rowOffset: 0,
  bgr: false,
  inverted: true
});

var screen = display.open(driver, {
  surface: {
    storage: "dma",
    fallbackStorage: "auto",
    chunkBytes: 16384,
    foreground: display.rgb565(255, 255, 255),
    background: display.rgb565(0, 0, 0),
    commandBuffer: {
      commandCapacity: 192,
      textBytes: 2048
    }
  },
  present: {
    merge: true
  },
  metrics: true
});

screen.clear();
screen.drawText(8, 8, "HELLO");
screen.present();
```

`display.open(driver, options)` is equivalent to:

```js
var screen = display.create(driver, options);
screen.open();
```

`display.create(...)` is useful when an application wants to render the first
frame before opening the hardware. The initial surface is dirty, and
`screen.open()` presents that frame after successful panel initialization.

### SSD1306

I2C bus settings and the panel address belong to the transport, not to the
controller or surface configuration:

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
  address: 0x3c,
  commandPrefix: 0x00,
  dataPrefix: 0x40
});

var driver = display.drivers.create("ssd1306", {
  transport: transport,
  width: 128,
  height: 64
});

var screen = display.open(driver, {
  surface: {
    storage: "auto",
    foreground: display.mono1(1),
    background: display.mono1(0),
    spacing: 0
  }
});

screen.clear();
screen.drawText(0, 0, "HELLO");
screen.present();
```

### Sharing a Bus

Supplied native handles are borrowed. Handles created by a transport are owned
by that transport.

```js
load("_sys/display/st7789.js");

var sharedBus = spi.openBus({
  host: spi.DEFAULT_HOST,
  sclk: spi.DEFAULT_SCLK,
  mosi: spi.DEFAULT_MOSI,
  miso: spi.DEFAULT_MISO,
  maxTransferSize: 16384
});

var transport = display.transports.create("spi4wire", {
  bus: sharedBus,
  deviceOptions: {
    cs: 2,
    mode: 0,
    freqHz: 40000000,
    queueSize: 2
  },
  pins: { dc: 4, reset: 5, backlight: 6 }
});

var driver = display.drivers.create("st7789", {
  transport: transport,
  width: 240,
  height: 240
});
var screen = display.open(driver);

screen.drawText(8, 8, "SHARED BUS");
screen.present();
screen.close();

// The transport borrowed sharedBus, so it remains valid here.
print(JSON.stringify(sharedBus.status()));
sharedBus.close();
```

Ownership rules are fixed rather than inferred during close:

| Input to transport | Transport behavior |
| --- | --- |
| no bus and no device handle | create and own both handles |
| caller-supplied bus, no device | borrow bus; create and own device |
| caller-supplied device | borrow device; do not close it |
| caller-supplied I2C bus | borrow bus; do not close it |

A transport always closes handles it created and never closes supplied handles.
There is no `ownBus` override in the first version; explicit caller cleanup is
safer than transferring ownership implicitly.

## Display Facade

`Display` is the object applications and `ui` use. It composes rather than
inherits:

- `screen.driver` is the actual `PanelDriver` object.
- `screen.surface` is the actual `Surface` object for diagnostics and advanced
  extensions.
- `screen.width`, `screen.height`, and `screen.pixelFormat` are read-only facade
  metadata.
- drawing methods delegate to `screen.surface` and return `screen` for chaining;
- value-returning methods such as `getPixel()` and `measureText()` return the
  surface result;
- presentation and panel-control methods delegate through the driver.

The primary facade methods are:

- `open()`
- `close()`
- common drawing methods (`clear`, `fillRect`, `drawText`, and the existing
  primitive set)
- `beginBatch(options?)` / `endBatch(batch)`
- `present(regions?, options?)`
- `flush()`, `flushRect(...)`, and `flushRects(...)` as explicit full/region
  convenience methods
- `supports(capability)`
- `setPower(enabled)`
- `setInverted(enabled)`
- `setContrast(value)`
- `setBacklight(enabled)`
- `stats()` / `resetStats()`

`present()` is the preferred new name because it describes copying a rendered
frame to a panel. With no explicit region it presents the surface's dirty
bounds. It clears native dirty state only after a successful transfer.
`flush()` always requests a full-screen transfer. Region methods remain useful
to `ui` and performance-sensitive applications.

The facade implements control methods consistently. A method throws a clear
unsupported-capability error when the driver does not advertise that feature;
callers can avoid that error with `supports(...)`.

```js
if (screen.supports("backlight")) {
  screen.setBacklight(false);
}
```

## Surface Contract

`Surface` becomes a concrete, hardware-independent renderer around one native
`DisplayBuffer`. It is no longer a base class for panel drivers.

It owns:

- the native framebuffer;
- foreground/background color normalization by pixel format;
- font lookup, encoding, drawing, and measurement;
- all common drawing primitive forwarding in one implementation;
- native command-buffer creation and replay;
- dirty state and frame-source export;
- surface resource cleanup.

It does not expose `open()`, `present()`, panel controls, or transport handles.

The display derives width, height, pixel format, layout, and transfer byte order
from driver metadata. Surface options cannot override those fields, preventing
buffer/panel mismatches. Driver implementations receive a narrow frame-source
object during presentation rather than owning or constructing the surface.

Command batching belongs here because it accelerates rendering into a
framebuffer and is not ST7789-specific. A future RGB565 panel can reuse the same
batch path without duplicating ST7789 code.

## Panel Driver Contract

A driver factory returns an uninitialized controller object with this logical
contract:

```text
name                  stable controller name
state                 created | open | closed
transport             one DisplayTransport
width, height          logical dimensions after rotation
pixelFormat            mono1 | rgb565
layout                 linear | page-y8
byteOrder              transfer order required by the controller
capabilities           explicit immutable capability map
open()                 initialize transport and controller
present(frame, regions, options)
setPower(enabled)
setInverted(enabled)
setContrast(value)
setBacklight(enabled)
close()                idempotent controller/transport cleanup
```

The exact JavaScript declaration will use interfaces rather than requiring a
shared prototype. A third-party driver may be a constructor instance or a plain
object returned by a factory as long as it satisfies the contract.

A panel driver must not:

- inherit from `display.Surface`;
- allocate a `DisplayBuffer`;
- implement `drawText`, `fillRect`, or other drawing methods;
- load fonts;
- decide UI dirty regions;
- directly open `spi` or `i2c` when a transport was supplied.

### Built-in Driver Responsibilities

The new SSD1306 driver contains only:

- geometry/page metadata;
- initialization and control commands;
- full/page-aligned presentation;
- power, inversion, and contrast commands.

The new ST7789 driver contains only:

- geometry, rotation, offsets, and MADCTL calculation;
- reset and initialization sequence;
- address-window commands;
- RGB565 region transfer with span-source/chunk fallbacks;
- power, inversion, and backlight controls.

Dirty-region merging moves to `Display`; command batching moves to `Surface`;
SPI/GPIO mechanics move to `SPI4WireTransport`.

## Transport Contract

Transports adapt native bus modules to panel-oriented byte operations. Built-in
transports are registered under `display.transports`:

- `i2c`
- `spi4wire`

The logical contract is:

```text
kind
state                 created | open | closed
capabilities          chunks, source, reset, backlight
open()
command(command, data?)
write(data, options?)
writeChunks(chunks, options?)
writeSource(source, options?)
reset()
setBacklight(enabled)
stats()
resetStats()
close()
```

A driver chooses the best available transfer capability in this order:

1. retained native span source;
2. reusable native chunks;
3. one bounded byte payload.

`SPI4WireTransport` owns DC state changes and control-pin configuration.
`I2CTransport` owns the device address and command/data prefix framing. Both
retain the existing bounded synchronous native operations and watchdog-safe
behavior.

GPIO pins configured by a managed transport are driven to a safe state and
released during `close()`. A custom transport can be used when an application
needs different pin ownership or reset sequencing.

## Registries and Selective Loading

The global module exposes explicit registries:

```text
display.transports.register(name, factory)
display.transports.create(name, options)
display.transports.list()

display.drivers.register(name, factory)
display.drivers.create(name, options)
display.drivers.list()

display.profiles.register(name, factory)
display.profiles.create(name, options)
display.profiles.open(name, options)
display.profiles.list()
```

Recommended entry points:

| Entry point | Loads |
| --- | --- |
| `_sys/display.js` | facade, surface, fonts, registries only |
| `_sys/display/ssd1306.js` | core + I2C transport + SSD1306 driver |
| `_sys/display/st7789.js` | core + SPI4Wire transport + ST7789 driver |
| `_sys/display/wlk1501spi8p.js` | ST7789 entry point + WLK profile |
| `_sys/display/all.js` | all built-in transports, drivers, and profiles |

Each file remains guarded against repeated `load(...)`. Driver-specific entry
points are preferred on memory-constrained boards.

Third-party registration example:

```js
load("_sys/display.js");

display.drivers.register("example", function (options) {
  return makeExamplePanelDriver(options);
});
```

## Profiles Are Not Drivers

`wlk1501spi8p` moves from the driver registry to the profile registry. Its
profile supplies nested defaults for:

- ST7789 controller and 240x240 geometry;
- SPI bus and device settings;
- DC/reset/backlight wiring;
- DMA/chunk/command-buffer surface settings.

A profile creates the same normal `Display`, `PanelDriver`, and
`DisplayTransport` objects as explicit construction:

```js
load("_sys/display/wlk1501spi8p.js");

var screen = display.profiles.open("wlk1501spi8p", {
  transport: {
    deviceOptions: { freqHz: 80000000 }
  },
  driver: {
    rotation: 0
  },
  surface: {
    chunkBytes: 32768
  },
  display: {
    metrics: true
  }
});
```

Profiles perform a deep merge only across documented groups. Unknown keys are
rejected rather than silently copied into unrelated layers.

## Capabilities

Capabilities are immutable after driver creation. Initial names are:

| Capability | Owner | Meaning |
| --- | --- | --- |
| `partialPresent` | driver | arbitrary rectangular presentation |
| `multiRegion` | Display/driver | efficient separate-region presentation |
| `power` | driver | controller display on/off |
| `inversion` | driver | hardware inversion |
| `contrast` | driver | hardware contrast control |
| `backlight` | transport/driver | controllable backlight GPIO |
| `batch` | surface | native drawing command batch available |
| `directSource` | transport | retained span-source transfer available |

`Display.capabilities` combines the three layers, and
`Display.supports(name)` is the stable query. UI will use capabilities rather
than probing optional functions with `typeof`.

## Lifecycle and Failure Semantics

### States

```text
created -> opening -> open -> closing -> closed
                 \-> closed (open failure after rollback)
```

Rules:

- Constructors and registry factories perform validation only, except that
  `Surface` may allocate its native framebuffer.
- Drawing is allowed in `created` and `open` states.
- Presentation and panel controls require `open`.
- `open()` on an already-open display returns the same display.
- `close()` is idempotent and terminal. Reopening a closed object throws.
- Any operation other than `close()`, metadata reads, or statistics on a closed
  display throws `cannot use a closed Display`.
- An open failure closes every transport/native resource acquired during that
  attempt and leaves the `Display` closed.
- A presentation failure leaves the display open and preserves dirty state so
  the caller can retry or close.

### Close Order

`Display.close()` attempts every cleanup step even if one step reports an
error:

1. turn off a managed backlight when supported;
2. close the panel driver;
3. close the transport (idempotent when already closed by the driver);
4. close command buffers and frame sources;
5. close the native surface buffer;
6. mark the facade closed and release references.

The first error is rethrown only after all cleanup attempts complete.

## Presentation and Metrics

Region normalization and merging are generic `Display` policy. Drivers receive
already-clamped regions compatible with their capabilities. A driver may expand
regions to hardware alignment, for example SSD1306 page boundaries, but it must
report the actual transferred pixel/byte counts.

`Display.stats()` replaces ST7789-specific public performance fields with a
common schema:

```text
presents
regions
pixels
bytes
chunks
directTransfers
totalUs
prepareUs
panelUs
transferUs
```

Driver and transport-specific detail can appear under nested `driver` and
`transport` objects without changing the common counters. Metrics collection is
controlled by `Display` option `metrics`; disabled mode must avoid per-transfer
time reads.

`resetStats()` resets facade, driver, and transport counters. Command-batch
statistics remain available on the returned batch object through `stats()`.

## Configuration Boundaries

The new API rejects the old flat option bag. Options are grouped by owner:

```text
transport.busOptions       host/pins/frequency/timeout/max transfer
transport.deviceOptions    chip select/mode/frequency/queue
transport.pins             dc/reset/backlight
transport framing          address/commandPrefix/dataPrefix

driver                     width/height/rotation/offsets/BGR/inversion

surface                    storage/chunk size/colors/font spacing/batch capacity

display                    present policy/metrics
```

This removes collisions such as `freqHz`, makes types discriminated, and lets
profiles override one layer without accidentally changing another.

All built-in factories reject unknown keys. This is deliberate: misspelled
hardware options should fail before GPIO or bus initialization.

## Breaking Changes

The implementation should make these breaks explicit rather than maintain two
architectures:

| Old API | New API |
| --- | --- |
| `display.open({ driver: "st7789", ... })` | create transport + driver, then `display.open(driver, options)` |
| `display.create(options).init()` | `display.create(driver, options).open()` |
| driver subclass is the drawing surface | `Display` composes `Surface` and `PanelDriver` |
| `screen.driver` is a string | `screen.driver` is the driver object; use `screen.driver.name` |
| `display.registerDriver(...)` | `display.drivers.register(...)` |
| `display.listDrivers()` | `display.drivers.list()` |
| `wlk1501spi8p` driver alias | `display.profiles.open("wlk1501spi8p", ...)` |
| `screen.init()` | `screen.open()` |
| `screen.on()/off()/invert()/contrast()/backlight()` | consistent `setPower`, `setInverted`, `setContrast`, `setBacklight` |
| `screen.getPerf()/resetPerf()` | `screen.stats()/resetStats()` |
| optional-method capability probing | `screen.supports(name)` |

Passing a legacy flat object to `display.open(...)` should throw a targeted
migration error, for example:

```text
display.open() now expects a PanelDriver; create one with display.drivers.create()
```

No hidden conversion layer should remain after applications and documentation
are migrated.

## Proposed File Layout

```text
shared/flash_data/_sys/
  display.js                         core-only entry point
  display/
    all.js                           all built-ins entry point
    ssd1306.js                       SSD1306 entry point
    st7789.js                        ST7789 entry point
    wlk1501spi8p.js                  profile entry point
    core/
      namespace.js                   guards, registries, shared validation
      colors.js                      packed color helpers
      fonts.js                       EQF/font-set loading and encoding
      surface.js                     generic displayBuffer renderer
      display.js                     public facade and lifecycle
      present.js                     region normalization/merge policy
    transports/
      i2c.js
      spi4wire.js
    drivers/
      ssd1306.js
      st7789.js
    profiles/
      wlk1501spi8p.js
    fonts/
      mono5x7.eqf
```

Files may be combined if measured parse/runtime overhead favors fewer files,
but ownership boundaries must remain visible in code and tests.

## Implementation Sequence

### Phase 1: Contracts and Generic Rendering

- Add registries, `Surface`, `Display`, capabilities, states, and validation.
- Move duplicated drawing/color/text forwarding into `Surface`.
- Move command batching out of ST7789 and into `Surface`.
- Add a fake panel driver so lifecycle and presentation can be tested without
  display hardware.

### Phase 2: Transports

- Implement managed/borrowed ownership rules.
- Add I2C command/data framing.
- Add SPI4Wire command/data/source/chunk paths and GPIO cleanup.
- Add fake-transport tests for open rollback, close ordering, and statistics.

### Phase 3: SSD1306 Driver

- Port only controller commands and presentation logic.
- Validate full/page-aligned transfer behavior using a fake I2C transport.
- Validate on real hardware when available.

### Phase 4: ST7789 Driver and WLK Profile

- Port controller sequencing and region transfer.
- Preserve retained span-source and queued SPI fast paths.
- Move region merging to `Display` and rendering batches to `Surface`.
- Replace the WLK driver alias with a profile.
- Compare performance-demo counters and frame timings with the current baseline.

### Phase 5: Consumers and Public API

- Update `ui` to query `Display.capabilities` and call `present(...)`.
- Update all Demo application construction and metrics calls.
- Rewrite `docs/js-api.md` and TypeScript declarations.
- Remove old driver inheritance, flat configuration, old loaders, and deprecated
  methods in the same change set.
- Bump the JS display layer to `0.5.0` and update the changelog.

## Testing Plan

### Hardware-independent board tests

Use a fake driver and fake transport to verify:

- driver factories perform no native bus I/O;
- drawing before `open()` is presented as the first frame;
- state transitions and repeated `open()`/`close()` behavior;
- open-failure rollback and cleanup ordering;
- presentation failure preserves dirty state;
- supplied bus/device handles remain open after display close;
- internally created handles close exactly once;
- unsupported capabilities report deterministic errors;
- region clamping, merging, alignment, and dirty clearing;
- driver code has no drawing methods or native-buffer allocation;
- profiles construct the same object graph as explicit setup.

### Existing automated validation

- `python scripts/remote.py check-js`
- `python -m unittest discover -s tests/python -v`
- `python scripts/remote.py test --scope c`
- full offline JS/DEBUG_GC suite
- ESP32-S3 Minimal and Demo builds
- ESP32-C3 Minimal build

### Hardware validation

- SSD1306 initialization, full refresh, inversion, contrast, close, and reopen by
  creating a new object;
- ST7789 full and partial presentation, inversion, backlight, close, and new
  object reopen;
- WLK profile defaults;
- shared SPI bus with a second device;
- display performance demo with direct span-source and command batching enabled;
- repeated create/open/present/close cycles while checking free heap.

## Acceptance Criteria

The redesign is complete only when:

1. No built-in panel driver inherits from `Surface`.
2. No built-in panel driver defines common drawing primitives.
3. No built-in panel driver creates a native `DisplayBuffer`.
4. SPI/I2C/GPIO access occurs only through a transport inside driver code.
5. A supplied SPI/I2C handle survives `Display.close()`.
6. All resources created by a display are explicitly released exactly once.
7. `ui` renders through the `Display` facade without driver-specific checks.
8. ST7789 retains the native command-buffer and direct span-source fast paths.
9. `wlk1501spi8p` appears in profiles and not in the driver registry.
10. Flat legacy configuration is absent from implementation, docs, declarations,
    and applications.
11. Exact MQuickJS syntax validation and all applicable automated tests pass.
12. Measured ST7789 frame and flush times do not regress materially from the
    recorded pre-refactor baseline.
