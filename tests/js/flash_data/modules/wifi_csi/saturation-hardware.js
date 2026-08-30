test("wifi_csi/saturation-hardware", function () {
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
  var i;

  test.equal(caps.supports.promiscuous, true,
    "pool saturation requires the promiscuous Build Context gate");
  try {
    try { wifi.disconnect(); } catch (ignoredDisconnectError) {}
    session = wifiCsi.open({
      source: "promiscuous",
      channel: "current",
      conflict: "fail",
      capture: capture,
      queue: { capacity: 2, overflow: "drop-newest" }
    });
    status = session.status();

    for (i = 0; i < status.effective.poolCapacity; i += 1) {
      probe = session.receive(i === 0 ? 8000 : 3000);
      test.ok(probe !== null,
        "ambient traffic should fill every configured CSI pool slot");
      frames.push(probe);
      probe = null;
    }

    stats = session.stats();
    test.equal(stats.leasedFrames, status.effective.poolCapacity,
      "retained frames should own the complete fixed pool");
    test.equal(stats.freePoolSlots, 0,
      "the saturated fixed pool should report no free slot");
    dropsBefore = stats.droppedPoolFull;
    probe = session.receive(8000);
    test.equal(probe, null,
      "callbacks cannot enqueue another frame while every slot is retained");
    stats = session.stats();
    test.ok(stats.droppedPoolFull > dropsBefore,
      "pool saturation should increment its dedicated drop counter");
  } finally {
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
    releasedSlots: releasedStats.freePoolSlots
  };
});
