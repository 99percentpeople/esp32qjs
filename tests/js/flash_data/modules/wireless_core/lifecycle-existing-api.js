test("wireless_core/lifecycle-existing-api", function () {
  var adapter = null;
  var scanner = null;
  var now = null;
  var csi = null;
  var scanDrops = 0;
  var caps = wifi.csi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var before;
  var after;
  var radioBefore;
  var radioAfter;
  var i;
  function check(value, message) {
    if (!value) throw new Error(message);
  }
  function settleTaskCleanup() {
    var attempt;
    var snapshot;
    var pending;
    var index;
    // Self-deleting FreeRTOS tasks release their stacks from the idle task.
    for (attempt = 0; attempt < 50; attempt += 1) {
      sleep(20);
      snapshot = sys.tasks();
      check(!snapshot.truncated, "task snapshot incomplete during cleanup");
      pending = false;
      for (index = 0; index < snapshot.tasks.length; index += 1) {
        if (snapshot.tasks[index].state === "deleted") pending = true;
      }
      if (!pending) return;
    }
    throw new Error("native task cleanup did not settle within 1000 ms");
  }
  function cycle() {
    try {
      adapter = ble.open({ roles: ["central"], maxConnections: 1 });
      scanner = adapter.scan({ active: false, durationMs: 100, capacity: 1 });
      sleep(150);
      scanDrops += scanner.stats().dropped;
      check(scanner.close() === true, "scanner close");
      scanner = null;
      check(adapter.close() === true, "adapter close");
      adapter = null;
      now = espNow.open({ receiveCapacity: 1, sendTimeoutMs: 250 });
      check(now.status().open === true, "ESP-NOW open");
      now.close();
      now = null;
      csi = wifi.csi.open({ source: "associated", channel: "current",
        conflict: "fail", capture: capture,
        queue: { capacity: 1, overflow: "drop-newest" } });
      csi.close();
      check(csi.stats().leasedFrames === 0, "CSI owner balance");
      csi = null;
    } finally {
      if (csi !== null) csi.close();
      if (now !== null) now.close();
      if (scanner !== null) scanner.close();
      if (adapter !== null) adapter.close();
    }
  }
  cycle();
  gc();
  settleTaskCleanup();
  gc();
  before = test.memorySnapshot();
  radioBefore = wifi.status().radio;
  for (i = 0; i < 5; i += 1) { cycle(); gc(); }
  settleTaskCleanup();
  gc();
  after = test.memorySnapshot();
  radioAfter = wifi.status().radio;
  check(radioAfter.clients.espNow === 0, "ESP-NOW radio lease leaked");
  check(radioAfter.clients.wifiCsi === 0, "CSI radio lease leaked");
  check(radioAfter.clients.total === radioBefore.clients.total, "radio owner imbalance");
  check(after.manager.managedPsramBytes === before.manager.managedPsramBytes,
    "managed PSRAM payload imbalance");
  check(after.manager.managedInternalBytes === before.manager.managedInternalBytes,
    "managed internal payload imbalance");
  return { cycles: 5, warmupCycles: 1, scanDrops: scanDrops,
    before: before, after: after, radio: radioAfter };
});
