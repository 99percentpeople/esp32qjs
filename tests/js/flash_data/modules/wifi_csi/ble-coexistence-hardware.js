test("wifi_csi/ble-coexistence-hardware", function () {
  var caps = wifiCsi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var adapter = null;
  var scanner = null;
  var report = null;
  var session = null;
  var frame = null;
  var before;
  var after;
  var stats;
  var scannerStats;
  var startMs;
  var csiFrames = 0;
  var bleReports = 0;

  test.equal(caps.supports.promiscuous, true,
    "BLE coexistence requires the promiscuous Build Context gate");
  gc();
  before = sys.status.memory;
  try {
    try { wifi.disconnect(); } catch (ignoredDisconnectError) {}
    adapter = ble.open({
      roles: ["central"],
      deviceName: "ESP32QJS-CSI-Test",
      preferredMtu: 128,
      maxConnections: 1
    });
    scanner = Future.call(adapter.scan, adapter, [{
      active: false,
      durationMs: 5000,
      capacity: 8
    }]).wait(5000);
    session = wifiCsi.open({
      source: "promiscuous",
      channel: "current",
      conflict: "fail",
      capture: capture,
      queue: { capacity: 8, overflow: "drop-newest" }
    });

    startMs = sys.millis();
    while (sys.millis() - startMs < 5000) {
      frame = session.receive(250);
      if (frame !== null) {
        csiFrames += 1;
        frame.close();
        frame = null;
      }
      report = scanner.receive(0);
      if (report !== null) {
        bleReports += 1;
        report.data.close();
        report = null;
      }
    }
    stats = session.stats();
    scannerStats = scanner.stats();
    test.ok(stats.callbacks > 0 && csiFrames > 0,
      "CSI callbacks should continue while the BLE controller scans");
    test.equal(stats.leasedFrames, 0,
      "BLE coexistence should close every delivered CSI frame");
  } finally {
    if (report !== null) report.data.close();
    if (frame !== null) frame.close();
    if (session !== null) session.close();
    if (scanner !== null) scanner.close();
    if (adapter !== null) Future.call(adapter.close, adapter, []).wait(12000);
    try { wifi.disconnect(); } catch (ignoredFinalDisconnectError) {}
  }

  gc();
  after = sys.status.memory;
  test.equal(wifi.status().radio.clients.wifiCsi, 0,
    "BLE coexistence close should release the CSI radio lease");
  test.ok(after.internal.largestFreeBlockBytes + 131072 >=
    before.internal.largestFreeBlockBytes,
    "BLE and CSI close should preserve a usable internal heap block");

  return {
    target: caps.target,
    csiFrames: csiFrames,
    csiCallbacks: stats.callbacks,
    csiAccepted: stats.accepted,
    bleReports: bleReports,
    bleQueued: scannerStats.queued,
    bleDropped: scannerStats.dropped,
    internalLargestDelta: after.internal.largestFreeBlockBytes -
      before.internal.largestFreeBlockBytes,
    dmaLargestDelta: after.dma.largestFreeBlockBytes -
      before.dma.largestFreeBlockBytes,
    psramLargestDelta: before.psram === null || after.psram === null
      ? null
      : after.psram.largestFreeBlockBytes - before.psram.largestFreeBlockBytes
  };
});
