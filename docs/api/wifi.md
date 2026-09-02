# `wifi` Module

Wi-Fi credentials are kept in RAM. Rebooting the board clears the active station config.
`wifi` owns 802.11 Station and shared-radio controls. IP addresses, routes,
interface readiness, and later Ethernet/PPP state belong exclusively to
`net.status()` and `net.watch()`.

- `wifi.DEFAULT_TIMEOUT_MS`
  Default station connect timeout in milliseconds.
- `wifi.status()`
  Return `{ initialized, started, connected, scanning, ssid, lastDisconnectReason, lastDisconnectReasonName, droppedDriverEvents, radio }`.
  `droppedDriverEvents` counts bounded internal Wi-Fi event publications that
  could not be queued without blocking.
  `started` is the boot-scoped physical-radio state rather than the station
  helper's event history. `radio` contains `{ generation, initialized,
  starting, started, mode, channel, channelGeneration, maxTxPowerDbm,
  powerSave, clients }`; client counts distinguish the Wi-Fi station helper
  from ESP-NOW leases.
- `wifi.setPowerSave(mode)`
  Set Station modem sleep to `"none"`, `"minimum"`, or `"maximum"` and return
  the active mode. This is separate from ESP-NOW connectionless wake-window
  control.
- `wifi.setTxPower(dbm)`
  Set the shared radio maximum TX power in 0.25 dBm increments from 2 through
  20. It returns the ESP-IDF-mapped actual dBm; read the same value from
  `wifi.status().radio.maxTxPowerDbm` after the radio is started.
- `wifi.connect(ssid, options?)`
  Start Station mode through the native Future driver. `ssid` is a required
  string; optional `password`, `timeoutMs`, `bssid`, `channel`, `scanMethod`,
  `sortMethod`, `minimumRssi`, `minimumAuthMode`, and `pmf` control association.
  Unknown fields are rejected. The returned object is Wi-Fi link/control
  status; read assigned addresses and default route through `net.status()`.
- `wifi.disconnect(timeoutMs = wifi.DEFAULT_TIMEOUT_MS)`
  Disconnect through the native Future driver and return only after the
  station state has converged to disconnected. Once submitted to ESP-IDF the
  disconnect side effect cannot be cancelled; a queued call remains
  cancellable before it starts.
- `wifi.scan(options?)`
  Run an event-driven AP scan and return an array of
  `{ ssid, bssid, rssi, channel, authMode, hidden }`. Options are `channel`,
  `showHidden`, `passive`, `dwellMs`, and `timeoutMs`; unknown fields are
  rejected. Hidden beacon records have an empty `ssid`.
- `wifi.csi`
  Optional bounded Channel State Information capture namespace. It exists only
  when `sys.info.features.wifiCsi` is true; see the Wi-Fi CSI API document for
  `capabilities()`, `open()`, layouts, ownership, and transport format.

Example:

```js
print(JSON.stringify(wifi.status()));
var aps = wifi.scan({ showHidden: true });
print(aps.length);
wifi.setPowerSave("minimum");
wifi.connect("your-ssid", { password: "your-password" });
print(JSON.stringify(net.status()));
var clock = sys.time.sync({
  servers: ["pool.ntp.org", "time.cloudflare.com"],
  timeoutMs: 10000
});
print(clock.unixTimeMs);
var nextScan = Future.call(wifi.scan, wifi, [{ passive: true }]);
print(nextScan.wait(10000).length);
print(JSON.stringify(wifi.status()));
wifi.disconnect();
```

The firmware does not embed a time provider; Agent/workspace policy supplies
the server list. Complete `sys.time.sync(...)` after connecting and before any
public HTTPS, TLS, or secure WebSocket operation so certificate validity dates
are checked against a current clock. SNTP provides ordinary wall-clock setup,
not authenticated time: it does not defend against an active network attacker
who can tamper with both DNS/network traffic and time synchronization.
After synchronization, `Date.now()` and `new Date()` use the same wall clock;
`sys.millis()`, `sys.micros()`, and `performance.now()` remain monotonic uptime
clocks and are not affected by SNTP adjustments.
