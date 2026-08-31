test("wifi_csi/throughput-hardware", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var caps = wifiCsi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var session = null;
  var batch = null;
  var source = null;
  var before;
  var after;
  var stats;
  var status;
  var startMs;
  var elapsedMs;
  var batches = 0;
  var frames = 0;
  var transportBytes = 0;
  var drops;
  var durationMs = 10000;
  var batchFrames = caps.limits.maxBatchFrames < 16
    ? caps.limits.maxBatchFrames : 16;

  test.equal(caps.supports.promiscuous, true,
    "throughput qualification requires the promiscuous Build Context gate");
  try {
    try { wifi.disconnect(); } catch (ignoredDisconnectError) {}
    wifi.connect(cfg.wifiSsid, {
      password: cfg.wifiPassword,
      timeoutMs: 15000
    });
    gc();
    before = sys.status.memory;
    session = wifiCsi.open({
      source: "promiscuous",
      channel: "current",
      conflict: "fail",
      capture: capture,
      queue: {
        capacity: caps.limits.maxQueueCapacity < 16
          ? caps.limits.maxQueueCapacity : 16,
        overflow: "drop-newest"
      }
    });
    status = session.status();
    startMs = sys.millis();
    while (sys.millis() - startMs < durationMs) {
      batch = session.receiveBatch({ maximumFrames: batchFrames, timeoutMs: 1000 });
      if (batch === null) {
        wifi.scan({
          channel: status.effective.channel,
          showHidden: true,
          passive: false,
          dwellMs: 250,
          timeoutMs: 3000
        });
        batch = session.receiveBatch({ maximumFrames: batchFrames, timeoutMs: 1000 });
      }
      if (batch !== null) {
        source = batch.source({ format: "esp32qjs-csi/1" });
        transportBytes += source.byteLength;
        frames += batch.frameCount;
        batches += 1;
        source.close();
        source = null;
        batch.close();
        batch = null;
      }
    }
    elapsedMs = sys.millis() - startMs;
    stats = session.stats();
    test.ok(stats.callbacks > 0,
      "throughput qualification requires real CSI callbacks");
    test.ok(stats.accepted > 0 && frames > 0,
      "throughput qualification requires delivered CSI frames");
  } finally {
    if (source !== null) source.close();
    if (batch !== null) batch.close();
    if (session !== null) session.close();
    try { wifi.disconnect(); } catch (ignoredFinalDisconnectError) {}
  }

  gc();
  after = sys.status.memory;
  drops = stats.droppedPoolFull + stats.droppedQueueFull +
    stats.droppedFrameTooLarge + stats.droppedClosing;
  test.equal(wifi.status().radio.clients.wifiCsi, 0,
    "throughput close should release the CSI radio lease");

  return {
    target: caps.target,
    schema: caps.configSchema,
    source: status.effective.source,
    channel: status.effective.channel,
    durationMs: elapsedMs,
    cpuFrequencyHz: sys.status.cpu.frequencyHz,
    callbacks: stats.callbacks,
    accepted: stats.accepted,
    deliveredFrames: frames,
    deliveredBatches: batches,
    receivedBytes: stats.receivedBytes,
    batchTransportBytes: transportBytes,
    callbackRateHz: stats.callbacks * 1000 / elapsedMs,
    acceptedRateHz: stats.accepted * 1000 / elapsedMs,
    receiveRateHz: frames * 1000 / elapsedMs,
    batchTransportBytesPerSecond: transportBytes * 1000 / elapsedMs,
    dropRatio: stats.callbacks === 0 ? 0 : drops / stats.callbacks,
    queueDepth: stats.queue.queued,
    queueDropped: stats.queue.dropped,
    memoryBefore: {
      internalFree: before.internal.freeBytes,
      internalMinimum: before.internal.minimumFreeBytes,
      internalLargest: before.internal.largestFreeBlockBytes,
      dmaFree: before.dma.freeBytes,
      dmaMinimum: before.dma.minimumFreeBytes,
      dmaLargest: before.dma.largestFreeBlockBytes,
      psramFree: before.psram === null ? null : before.psram.freeBytes,
      psramMinimum: before.psram === null ? null : before.psram.minimumFreeBytes,
      psramLargest: before.psram === null ? null : before.psram.largestFreeBlockBytes
    },
    memoryAfter: {
      internalFree: after.internal.freeBytes,
      internalMinimum: after.internal.minimumFreeBytes,
      internalLargest: after.internal.largestFreeBlockBytes,
      dmaFree: after.dma.freeBytes,
      dmaMinimum: after.dma.minimumFreeBytes,
      dmaLargest: after.dma.largestFreeBlockBytes,
      psramFree: after.psram === null ? null : after.psram.freeBytes,
      psramMinimum: after.psram === null ? null : after.psram.minimumFreeBytes,
      psramLargest: after.psram === null ? null : after.psram.largestFreeBlockBytes
    }
  };
});
