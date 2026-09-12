(function () {
  var key = __HOLD_KEY__;
  var mode = __CASE_MODE__;
  var held = {};
  var frame = null;
  if (Object.prototype.hasOwnProperty.call(globalThis, key)) throw new Error("owned key already exists");
  wifi.disconnect();
  wifi.stop({ timeoutMs: 5000 });
  try {
    if (mode === "monitor") {
      held.monitor = wifi.monitor.open({ filter: { types: ["management"] },
        capture: { snapLength: 128 }, buffering: { poolCapacity: 2, queueCapacity: 1 } });
      frame = held.monitor.receive(5000);
      if (frame === null) throw new Error("ambient packet required for retained storage proof");
      held.view = frame.bytes();
      held.source = frame.source();
      held.first = held.view.getUint8(0);
      frame.close(); frame = null;
      held.monitor.stop();
      held.pending = Future.call(held.monitor.receive, held.monitor, [60000]);
    } else {
      held.now = espNow.open({ receiveCapacity: 2, sendTimeoutMs: 250 });
      var caps = wifi.csi.capabilities();
      var capture = caps.configSchema === "wifi-csi-he/1"
        ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
        : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
      held.csi = wifi.csi.open({ source: { mode: "associated" }, capture: capture,
        buffering: { poolCapacity: 2, queueCapacity: 1 } });
      held.pending = Future.call(held.csi.receive, held.csi, [60000]);
      held.nowPending = Future.call(held.now.receive, held.now, [60000]);
    }
    globalThis[key] = held;
    Future.sleep(10).wait(1000);
    gc();
    if (held.pending.status() !== "pending") throw new Error("native receive must remain pending at restart");
    if (held.nowPending && held.nowPending.status() !== "pending") throw new Error("ESP-NOW receive must remain pending");
    if (held.view && held.view.getUint8(0) !== held.first) throw new Error("retained View corrupted before restart");
    return { mode: mode, bootId: sys.status.boot.bootId,
      generation: sys.status.runtime.generation, pending: held.pending.status(),
      viewBytes: held.view ? held.view.byteLength : 0,
      sourceBytes: held.source ? held.source.byteLength : 0,
      radio: wifi.status().radio, diagnostics: wifi.diagnostics.snapshot() };
  } catch (error) {
    if (frame !== null) frame.close();
    if (held.source) held.source.close();
    if (held.view) held.view.close();
    if (held.monitor) held.monitor.close();
    if (held.csi) held.csi.close();
    if (held.now) held.now.close();
    delete globalThis[key];
    throw error;
  }
})()
