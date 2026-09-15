test("wifi/monitor-retained-hardware", function () {
  function settle(session) {
    var i;
    for (i = 0; i < 100; i += 1) {
      try { session.close(); }
      catch (error) { if (!session.status().cleanupPending) throw error; }
      if (!session.status().cleanupPending) return;
      Future.sleep(10).wait(1000);
    }
    throw new Error("Monitor close did not settle");
  }
  var volume = fs;
  var path = "wifi-monitor-retained-" + sys.millis() + ".bin";
  var session = null;
  var batch = null;
  var view = null;
  var source = null;
  var stream = null;
  var fileOwned = false;
  var beforeOwners = wifi.status().radio.clients.wifiMonitor;
  var first;
  var bytes;
  var queued;
  var saturated;
  var result;
  test.equal(volume.exists(path), false, "test output must not overwrite an existing file");
  try {
    session = wifi.monitor.open({
      filter: { types: ["management"], subtypes: [8] },
      capture: { snapLength: 128 },
      buffering: { poolCapacity: 2, queueCapacity: 1 }
    });
    Future.sleep(1500).wait(2500);
    queued = session.stats();
    batch = session.receiveBatch({ maximumFrames: 2, minimumFrames: 2,
      timeoutMs: 5000, maximumLatencyMs: 2000 });
    if (batch === null || batch.frameCount !== 2) {
      test.equal(session.stats().filtered.invalidCallback, 0,
        "native callbacks must not all be rejected as invalid metadata");
      test.skip("ambient beacons did not fill the two-frame Batch");
    }
    test.ok(queued.droppedQueueFull > 0, "full observation queue records packet drops");
    view = batch.bytes(0);
    first = view.getUint8(0);
    source = batch.source({ format: "esp32qjs-monitor/1" });
    bytes = source.byteLength;
    saturated = session.stats();
    test.equal(saturated.freeSlots, 0, "retained Batch saturates the two-slot pool");
    Future.sleep(500).wait(1500);
    test.ok(session.stats().droppedPoolFull > saturated.droppedPoolFull,
      "pool saturation records drops while control completion stays available");
    result = { frames: batch.frameCount, queueDrops: queued.droppedQueueFull,
      poolDrops: session.stats().droppedPoolFull, wireBytes: bytes };
    settle(session);
    test.equal(wifi.status().radio.clients.wifiMonitor, beforeOwners,
      "close returns the Radio owner with payloads still retained");
    batch.close();
    batch = null;
    gc();
    test.equal(view.getUint8(0), first, "View survives Batch/Session close and GC");
    test.equal(session.status().poolRetained, true, "View/Source keep the original pool alive");
    view.close();
    view = null;
    gc();
    test.equal(session.status().poolRetained, true, "Source independently retains its payloads");
    fileOwned = true;
    stream = volume.open(path, "wb");
    test.equal(stream.write(source), bytes, "retained wire Source streams after GC");
    stream.close();
    stream = null;
    source.close();
    source = null;
    gc();
    test.equal(session.status().poolRetained, false, "last owner returns the original pool");
    test.equal(volume.stat(path).size, bytes, "stream writes the complete bounded wire record");
    return result;
  } finally {
    if (stream !== null) stream.close();
    if (source !== null) source.close();
    if (view !== null) view.close();
    if (batch !== null) batch.close();
    if (session !== null) settle(session);
    if (fileOwned && volume.exists(path)) volume.remove(path);
  }
});
