test("wifi_csi/associated-hardware", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var caps = wifi.csi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var session = null;
  var frame = null;
  var samples = null;
  var copy = null;
  var source = null;
  var trafficClient = null;
  var trafficResponse = null;
  var trafficRequestText =
    "GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";
  var trafficRequest = [];
  var preTrafficFrames = 0;
  var trafficResponseBytes = 0;
  var frameBytes = 0;
  var frameSourceMac = "";
  var frameRssi = 0;
  var requestIndex;
  var status;
  var stats;

  try {
    try { wifi.disconnect(); } catch (ignoredDisconnectError) {}
    wifi.connect(cfg.wifiSsid, {
      password: cfg.wifiPassword,
      timeoutMs: 15000
    });
    session = wifi.csi.open({
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
    while ((frame = session.receive(0)) !== null) {
      preTrafficFrames += 1;
      frame.close();
      frame = null;
    }
    trafficClient = socket.openTCP({ localPort: 0 });
    test.ok(trafficClient.connect("example.com", 80, { timeoutMs: 10000 }),
      "associated CSI traffic probe should connect");
    for (requestIndex = 0; requestIndex < trafficRequestText.length;
         requestIndex += 1) {
      trafficRequest.push(trafficRequestText.charCodeAt(requestIndex));
    }
    test.ok(trafficClient.send(trafficRequest, 5000) > 0,
      "associated CSI traffic probe should send a request");
    trafficResponse = trafficClient.recv(512, 5000);
    test.ok(trafficResponse !== null,
      "associated CSI traffic probe should receive response bytes");
    trafficResponseBytes = trafficResponse.byteLength;
    test.ok(trafficResponseBytes > 0,
      "associated CSI traffic probe should receive non-empty response bytes");
    trafficResponse.close();
    trafficResponse = null;
    trafficClient.close();
    trafficClient = null;
    frame = session.receive(8000);
    test.ok(frame !== null,
      "explicit router traffic should produce a CSI frame");
    test.ok(frame.info.layout.byteLength > 0,
      "captured CSI should contain IQ bytes");
    test.equal(frame.info.layout.componentOrder, "imaginary-real",
      "raw component order should be explicit");
    frameBytes = frame.info.layout.byteLength;
    frameSourceMac = frame.info.sourceMac;
    frameRssi = frame.info.rssi;
    samples = frame.samples();
    copy = frame.copySamples();
    source = frame.source();
    test.equal(samples.byteLength, frame.info.layout.byteLength,
      "retained samples should expose the complete pool payload");
    test.equal(copy.byteLength, frame.info.layout.byteLength,
      "owned copies should preserve the complete payload");
    test.ok(source.byteLength === frame.info.layout.byteLength,
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
    if (trafficResponse !== null) trafficResponse.close();
    if (trafficClient !== null) trafficClient.close();
    if (session !== null) session.close();
    try { wifi.disconnect(); } catch (ignoredFinalDisconnectError) {}
  }
  test.equal(wifi.status().radio.clients.wifiCsi, 0,
    "associated close should release the CSI radio lease");

  return {
    target: caps.target,
    schema: caps.configSchema,
    preTrafficFrames: preTrafficFrames,
    trafficResponseBytes: trafficResponseBytes,
    frameBytes: frameBytes,
    frameSourceMac: frameSourceMac,
    frameRssi: frameRssi
  };
});
