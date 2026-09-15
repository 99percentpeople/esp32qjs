test("wifi/monitor-batch-hardware", function () {
  function settle(value, closing) {
    var attempt;
    var state;
    for (attempt = 0; attempt < 100; attempt += 1) {
      try { if (closing) value.close(); else value.stop(); }
      catch (error) { if (!value.status().cleanupPending) throw error; }
      state = value.status();
      if (!state.cleanupPending && state.state === (closing ? "closed" : "stopped")) return;
      Future.sleep(10).wait(1000);
    }
    throw new Error("Monitor cleanup did not settle");
  }
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var session = null;
  var batch = null;
  var view = null;
  var source = null;
  var stream = null;
  var sourceLength;
  var expectedLength;
  var frameIndex;
  var packetView;
  var first;
  var info;
  var rejected;
  var volume = fs;
  var outputPath = "wifi-monitor-batch-" + sys.millis() + ".bin";
  var outputOwned = false;
  test.equal(volume.exists(outputPath), false, "test output must not replace a workspace file");
  try {
    wifi.connect(cfg.wifiSsid, { password: cfg.wifiPassword, timeoutMs: 15000 });
    session = wifi.monitor.open({
      channel: "current",
      filter: { types: ["management"], subtypes: [8] },
      capture: { snapLength: 128 },
      buffering: { poolCapacity: 4, queueCapacity: 4 }
    });
    batch = session.receiveBatch({ maximumFrames: 4, minimumFrames: 2, timeoutMs: 5000, maximumLatencyMs: 1000 });
    test.ok(batch !== null && batch.frameCount >= 1 && batch.frameCount <= 4,
      "configured AP channel supplies a bounded Beacon Batch");
    info = batch.info(0);
    test.equal(info.packet.category, "management", "batch metadata preserves driver type");
    test.equal(info.packet.frameType.subtype, 8, "batch metadata preserves Beacon subtype");
    view = batch.bytes(0);
    first = view.getUint8(0);
    rejected = false;
    try { batch.bytes(batch.frameCount); } catch (rangeError) { rejected = true; }
    test.ok(rejected, "index at frameCount is rejected");
    expectedLength = 32 + batch.frameCount * (24 + 256);
    for (frameIndex = 0; frameIndex < batch.frameCount; frameIndex += 1) {
      packetView = batch.bytes(frameIndex);
      expectedLength += Math.ceil(packetView.byteLength / 4) * 4;
      packetView.close();
    }
    rejected = false;
    try { batch.source({ format: "bad" }); } catch (formatError) { rejected = true; }
    test.ok(rejected, "wire format is exact");
    source = batch.source({ format: "esp32qjs-monitor/1" });
    sourceLength = source.byteLength;
    test.equal(sourceLength, expectedLength, "wire length includes metadata and final alignment");
    settle(session, false);
    session.configure({ filter: { types: [] }, buffering: { poolCapacity: 2, queueCapacity: 2 } });
    gc();
    test.equal(batch.info(0).sequence, info.sequence, "old batch remains on its original pool after configure");
    batch.close();
    batch.close();
    rejected = false;
    try { batch.info(0); } catch (closedError) { rejected = true; }
    test.ok(rejected, "closed Batch rejects access");
    batch = null;
    settle(session, true);
    gc();
    test.equal(view.getUint8(0), first, "retained View outlives Batch, old Session and replacement close");
    view.close();
    view = null;
    outputOwned = true;
    stream = volume.open(outputPath, "wb");
    test.equal(stream.write(source), sourceLength, "wire Source survives all public owners and GC");
    stream.close();
    stream = null;
    source.close();
    source = null;
  } finally {
    if (stream !== null) stream.close();
    if (source !== null) source.close();
    if (view !== null) view.close();
    if (batch !== null) batch.close();
    if (session !== null) settle(session, true);
    if (outputOwned && volume.exists(outputPath)) volume.remove(outputPath);
    wifi.disconnect();
  }
});
