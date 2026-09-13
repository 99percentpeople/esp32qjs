test("wifi/native-policy-hardware", function () {
  var original;
  var monitor = null;
  var csi = null;
  var tx = null;
  var frames = [];
  var caps = wifi.csi.capabilities();
  var type;
  var subtype;
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  test.equal(wifi.status().connected, false, "test requires a disconnected Station");
  wifi.start({ mode: "station", storage: "ram" });
  original = wifi.status().radio.powerSave;
  for (type = 0; type < 4; type += 1)
    for (subtype = 0; subtype < 16; subtype += 1)
      frames.push({ type: type, subtype: subtype });
  try {
    wifi.setPowerSave("minimum");
    monitor = wifi.monitor.open({ filter: { frames: frames, maximumRateHz: 0 },
      capture: { snapLength: 24 }, buffering: { poolCapacity: 2, queueCapacity: 2 } });
    test.equal(wifi.status().radio.powerSave, "minimum", "Monitor preserves caller power save");
    test.equal(monitor.status().state, "running", "Monitor starts with all numeric pairs and unlimited rate");
    monitor.close(); monitor = null;
    csi = wifi.csi.open({ capture: capture,
      filter: { frames: frames, maximumRateHz: 0 },
      buffering: { poolCapacity: 2, queueCapacity: 2 } });
    test.equal(wifi.status().radio.powerSave, "minimum", "CSI preserves caller power save");
    test.equal(csi.status().requested.filter.frames.length, 64, "CSI accepts all numeric frame pairs");
    csi.stop();
    csi.configure(csi.status().requested);
    test.equal(csi.status().requested.filter.maximumRateHz, 0, "CSI unlimited rate survives roundtrip");
    csi.close(); csi = null;
    tx = wifi.rawTx.open({ timeoutMs: 2147483647 });
    tx.close(); tx = null;
    wifi.disconnect({ timeoutMs: 2147483647 });
    gc();
    test.equal(wifi.status().radio.clients.wifiMonitor, 0, "Monitor owner returns");
    test.equal(wifi.status().radio.clients.wifiCsi, 0, "CSI owner returns");
    test.equal(wifi.status().radio.clients.wifiRawTx, 0, "Raw TX owner returns");
    return { numericFramePairs: frames.length, unlimitedRate: true, callerPowerSave: "minimum" };
  } finally {
    if (tx !== null) tx.close();
    if (csi !== null) csi.close();
    if (monitor !== null) monitor.close();
    if (original !== null) wifi.setPowerSave(original);
  }
});
