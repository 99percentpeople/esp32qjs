test("wifi_csi/associated-hardware", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var caps = wifiCsi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var session = null;
  var frame = null;
  var samples = null;
  var copy = null;
  var source = null;
  var status;
  var stats;

  try {
    try { wifi.disconnect(); } catch (ignoredDisconnectError) {}
    wifi.connect(cfg.wifiSsid, {
      password: cfg.wifiPassword,
      timeoutMs: 15000
    });
    session = wifiCsi.open({
      source: "associated",
      channel: "current",
      conflict: "fail",
      capture: capture,
      queue: { capacity: 8, overflow: "drop-newest" }
    });
    status = session.status();
    test.equal(status.state, "running", "associated CSI should start");
    test.ok(wifi.status().radio.clients.wifiCsi >= 1,
      "CSI should hold a visible radio lease");
    try {
      sys.time.sync({ servers: ["pool.ntp.org"], timeoutMs: 5000 });
    } catch (ignoredTimeError) {}
    frame = session.receive(8000);
    test.ok(frame !== null,
      "associated traffic should produce a CSI frame");
    test.ok(frame.info.layout.byteLength > 0,
      "captured CSI should contain IQ bytes");
    test.equal(frame.info.layout.componentOrder, "imaginary-real",
      "raw component order should be explicit");
    samples = frame.samples();
    copy = frame.copySamples();
    source = frame.source();
    test.equal(samples.byteLength, frame.info.layout.byteLength,
      "retained samples should expose the complete pool payload");
    test.equal(copy.byteLength, frame.info.layout.byteLength,
      "owned copies should preserve the complete payload");
    test.ok(rpc.sourceInfo(source).size === frame.info.layout.byteLength,
      "frame sources should expose their transport length");
    frame.close();
    frame = null;
    test.equal(samples.byteLength, copy.byteLength,
      "derived leases should survive frame close");
    stats = session.stats();
    test.ok(stats.callbacks >= stats.accepted,
      "callback counters should dominate accepted frames");
  } finally {
    if (source !== null) source.close();
    if (samples !== null) samples.close();
    if (copy !== null) copy.close();
    if (frame !== null) frame.close();
    if (session !== null) session.close();
    try { wifi.disconnect(); } catch (ignoredFinalDisconnectError) {}
  }
  test.equal(wifi.status().radio.clients.wifiCsi, 0,
    "associated close should release the CSI radio lease");

  return { target: caps.target, schema: caps.configSchema };
});
