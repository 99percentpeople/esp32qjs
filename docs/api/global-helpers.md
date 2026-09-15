# Global Helpers

## Global object

`globalThis` is the ESP32 MQuickJS global object and is always available. All
framework globals are properties of this object: for example,
`globalThis.sys === sys`, `globalThis.Future === Future`, and, when Wi-Fi is
compiled, `globalThis.wifi === wifi`. Optional module properties are absent
when their corresponding `sys.info.features` value is `false`.

Use `globalThis` when code needs an explicit global receiver or a persistent
application namespace.

```js
globalThis.application = {
  stop: function () {
    print("stopped");
  }
};
```

## Functions

- `help()`
  Print the location of these API documents.
- `print(...values)`
  Write values to the runtime's standard-output sink. Normal profiles use the
  serial console; the headless Agent profile forwards a bounded copy to its
  host diagnostics cache.
- `gc()`
  Run the JavaScript garbage collector explicitly for diagnostics. MQuickJS
  also collects automatically before a JavaScript heap allocation would
  exhaust its configured heap.
- `load(path)`
  Evaluate a script using the mounted filesystem namespace. Absolute paths
  always start at `/`. During script evaluation, relative nested loads use the
  owning mount of that script; outside a load context they start at `/`. For example, a system script uses
  `load("_sys/display/core.js")` for `/framework/_sys/display/core.js`;
  application scripts loaded from `/index.js` use the root mount instead.
  Paths are relative to the mount, not the calling script's directory.
  Load contexts are restored after success or failure.
- `framework.load(path)`
  Load a bundled library below `/framework/_sys`, for example
  `framework.load("display.js")`. Relative nested loads stay on `/framework`.
  An explicit absolute `load("/index.js")` enters the application mount for that
  script and its nested loads, then restores the system load context.
  Resource reads use ordinary namespace paths such as
  `fs.readText("/framework/_sys/fonts/map.json")`; global `fs` stays at `/`.
- `sleep(ms)`
  Wait for `ms` milliseconds while the Future scheduler, timers, deadlines,
  watchdog, and stop requests continue to advance.

Startup behavior:

- If `index.js` exists on the configured startup volume (normally `/framework/index.js`), it is loaded automatically before the first `js>` prompt appears.
- `index.js` is the single startup entry point. An external Build Context
  resolver may generate it from selected JavaScript Library entries.
- A Library entry may initialize a native-feature wrapper, register Board support, or decide whether and when to load writable application code.
- Optional examples can live under `demo/` and be started manually, for example `load("demo/display_perf.js")`.

Examples:

```js
print("hello");
gc();
sleep(50);
wifi.connect("your-ssid", {
  password: "your-password",
  timeoutMs: 10000
});
sys.time.sync({ servers: ["pool.ntp.org"], timeoutMs: 10000 });
print(http.fetch("https://example.com").status);
load("demo/display_perf.js");
print(Future.call(function () { return 123; }).wait(1000));
```

Example `index.js`:

```js
print("[startup] boot script running");
framework.load("vendor/device-support.js");
load("application/startup.js");
wifi.connect("your-ssid", {
  password: "your-password",
  timeoutMs: 10000
});
sys.time.sync({ servers: ["pool.ntp.org"], timeoutMs: 10000 });
```
