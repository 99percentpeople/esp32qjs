# Global Helpers

## Global object

`globalThis` is the ESP32 MQuickJS global object and is always available. All
framework globals are properties of this object: for example,
`globalThis.sys === sys`, `globalThis.Future === Future`, and, when Wi-Fi is
compiled, `globalThis.wifi === wifi`. Optional module properties are absent
when their corresponding `sys.info.features` value is `false`.

Use `globalThis` when code needs an explicit global receiver or a persistent
application namespace. Do not guess browser or Node.js aliases such as
`window`, `self`, or `global`; they are not ESP32QJS APIs.

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
  host diagnostics cache so protocol byte streams are not polluted.
- `gc()`
  Run the JavaScript garbage collector explicitly for diagnostics. Normal
  applications do not need to call it: MQuickJS automatically collects before
  a JavaScript heap allocation would exhaust its configured heap. The framework
  does not schedule additional collections from Future completion, native
  allocation pressure, or scheduler polling.
- `load(path)`
  Evaluate a script from the immutable volume currently stored in global `fs`.
  The initial volume is rooted at `/littlefs`; applications can install another
  mounted volume with `globalThis.fs = fs.volume(path)`.
- `framework.load(path)`
  Evaluate a bundled framework script below `/littlefs/_sys`, regardless of
  the active application root. Nested `load(...)` calls made while evaluating the
  framework module also remain on the system partition.
- `sleep(ms)`
  Wait for `ms` milliseconds while the Future scheduler, timers, deadlines,
  watchdog, and stop requests continue to advance.

Startup behavior:

- If `/littlefs/index.js` exists, it is loaded automatically before the first `js>` prompt appears.
- `index.js` is the single startup entry point. An external Build Context resolver may generate it from selected JavaScript Library entries; the framework does not interpret Library manifests.
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
