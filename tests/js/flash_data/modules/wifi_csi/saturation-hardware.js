test("wifi_csi/saturation-hardware", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var caps = wifiCsi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var session = null;
  var probe = null;
  var frames = [];
  var status;
  var stats;
  var releasedStats;
  var dropsBefore;
  var fillScanAttempts = 0;
  var dropScanAttempts = 0;
  var trafficClient = null;
  var trafficResponse = null;
  var trafficRequestText =
    "GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";
  var trafficRequest = [];
  var requestIndex;
  var i;

  test.equal(caps.supports.promiscuous, true,
    "pool saturation requires the promiscuous Build Context gate");
  try {
    try { wifi.disconnect(); } catch (ignoredDisconnectError) {}
    wifi.connect(cfg.wifiSsid, {
      password: cfg.wifiPassword,
      timeoutMs: 15000
    });
    session = wifiCsi.open({
      source: "promiscuous",
      channel: "current",
      conflict: "fail",
      capture: capture,
      queue: { capacity: 2, overflow: "drop-newest" }
    });
    status = session.status();

    while (frames.length < status.effective.poolCapacity &&
           fillScanAttempts < status.effective.poolCapacity * 12) {
      wifi.scan({
        channel: status.effective.channel,
        showHidden: true,
        passive: false,
        dwellMs: 250,
        timeoutMs: 3000
      });
      fillScanAttempts += 1;
      probe = session.receive(1000);
      while (probe !== null &&
             frames.length < status.effective.poolCapacity) {
        frames.push(probe);
        probe = session.receive(0);
      }
    }
    test.equal(frames.length, status.effective.poolCapacity,
      "active same-channel scans should fill every configured CSI pool slot");

    stats = session.stats();
    test.equal(stats.leasedFrames, status.effective.poolCapacity,
      "retained frames should own the complete fixed pool");
    test.equal(stats.freePoolSlots, 0,
      "the saturated fixed pool should report no free slot");
    dropsBefore = stats.droppedPoolFull;
    trafficClient = socket.openTCP({ localPort: 0 });
    test.ok(trafficClient.connect("example.com", 80, { timeoutMs: 10000 }),
      "pool-full traffic probe should connect");
    for (requestIndex = 0; requestIndex < trafficRequestText.length;
         requestIndex += 1) {
      trafficRequest.push(trafficRequestText.charCodeAt(requestIndex));
    }
    test.ok(trafficClient.send(trafficRequest, 5000) > 0,
      "pool-full traffic probe should send a request");
    trafficResponse = trafficClient.recv(512, 5000);
    test.ok(trafficResponse !== null,
      "pool-full traffic probe should receive response bytes");
    trafficResponse.close();
    trafficResponse = null;
    trafficClient.close();
    trafficClient = null;
    stats = session.stats();
    while (stats.droppedPoolFull <= dropsBefore &&
           dropScanAttempts < status.effective.poolCapacity * 12) {
      wifi.scan({
        channel: status.effective.channel,
        showHidden: true,
        passive: false,
        dwellMs: 250,
        timeoutMs: 3000
      });
      stats = session.stats();
      dropScanAttempts += 1;
    }
    probe = session.receive(0);
    test.equal(probe, null,
      "callbacks cannot enqueue another frame while every slot is retained");
    stats = session.stats();
    test.ok(stats.droppedPoolFull > dropsBefore,
      "pool saturation should increment its dedicated drop counter");
  } finally {
    if (trafficResponse !== null) trafficResponse.close();
    if (trafficClient !== null) trafficClient.close();
    if (session !== null) session.stop();
    if (probe !== null) probe.close();
    for (i = frames.length - 1; i >= 0; i -= 1) frames[i].close();
    if (session !== null) {
      releasedStats = session.stats();
      session.close();
      session = null;
    }
    try { wifi.disconnect(); } catch (ignoredFinalDisconnectError) {}
  }

  test.equal(releasedStats.leasedFrames, 0,
    "closing all retained frames should release every pool lease");
  test.equal(releasedStats.freePoolSlots, status.effective.poolCapacity,
    "closing all retained frames should return the complete pool");
  test.equal(wifi.status().radio.clients.wifiCsi, 0,
    "closing the saturated session should release the radio lease");

  return {
    target: caps.target,
    poolCapacity: status.effective.poolCapacity,
    callbacks: stats.callbacks,
    accepted: stats.accepted,
    droppedPoolFull: stats.droppedPoolFull,
    droppedQueueFull: stats.droppedQueueFull,
    fillScanAttempts: fillScanAttempts,
    dropScanAttempts: dropScanAttempts,
    releasedSlots: releasedStats.freePoolSlots
  };
});
