test("wifi_csi/soak-hardware", function () {
  var cfg = test.config();
  var caps = wifi.csi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var durationMs = typeof cfg.csiSoakDurationMs === "number" &&
      cfg.csiSoakDurationMs >= 60000 && cfg.csiSoakDurationMs <= 3600000
    ? cfg.csiSoakDurationMs : 300000;
  var session = null;
  var batch = null;
  var before;
  var sample;
  var after;
  var stats;
  var startMs;
  var nextSampleMs;
  var elapsedMs;
  var frames = 0;
  var batches = 0;
  var checkpoints = 0;
  var minimumInternalLargest;
  var minimumDmaLargest;
  var minimumPsramLargest;
  var batchFrames = caps.limits.maxBatchFrames < 16
    ? caps.limits.maxBatchFrames : 16;

  test.equal(caps.supports.promiscuous, true,
    "long soak requires the promiscuous Build Context gate");
  gc();
  before = sys.status.memory;
  minimumInternalLargest = before.internal.largestFreeBlockBytes;
  minimumDmaLargest = before.dma.largestFreeBlockBytes;
  minimumPsramLargest = before.psram === null
    ? null : before.psram.largestFreeBlockBytes;
  try {
    try { wifi.disconnect(); } catch (ignoredDisconnectError) {}
    session = wifi.csi.open({
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
    startMs = sys.millis();
    nextSampleMs = startMs + 10000;
    while (sys.millis() - startMs < durationMs) {
      batch = session.receiveBatch({ maximumFrames: batchFrames, timeoutMs: 1000 });
      if (batch !== null) {
        frames += batch.frameCount;
        batches += 1;
        batch.close();
        batch = null;
      }
      if (sys.millis() >= nextSampleMs) {
        gc();
        sample = sys.status.memory;
        if (sample.internal.largestFreeBlockBytes < minimumInternalLargest) {
          minimumInternalLargest = sample.internal.largestFreeBlockBytes;
        }
        if (sample.dma.largestFreeBlockBytes < minimumDmaLargest) {
          minimumDmaLargest = sample.dma.largestFreeBlockBytes;
        }
        if (minimumPsramLargest !== null && sample.psram !== null &&
            sample.psram.largestFreeBlockBytes < minimumPsramLargest) {
          minimumPsramLargest = sample.psram.largestFreeBlockBytes;
        }
        checkpoints += 1;
        nextSampleMs += 10000;
      }
    }
    elapsedMs = sys.millis() - startMs;
    stats = session.stats();
    test.ok(stats.callbacks > 0 && stats.accepted > 0 && frames > 0,
      "long soak requires sustained real CSI traffic");
    test.equal(stats.leasedFrames, 0,
      "every soak batch must release its retained frame leases");
  } finally {
    if (batch !== null) batch.close();
    if (session !== null) session.close();
    try { wifi.disconnect(); } catch (ignoredFinalDisconnectError) {}
  }

  gc();
  after = sys.status.memory;
  test.equal(wifi.status().radio.clients.wifiCsi, 0,
    "long soak close should release the CSI radio lease");
  test.ok(after.internal.largestFreeBlockBytes + 131072 >=
    before.internal.largestFreeBlockBytes,
    "long soak close should return internal heap near its baseline");
  test.ok(after.dma.largestFreeBlockBytes + 131072 >=
    before.dma.largestFreeBlockBytes,
    "long soak close should return DMA heap near its baseline");
  if (before.psram !== null && after.psram !== null) {
    test.ok(after.psram.largestFreeBlockBytes + 262144 >=
      before.psram.largestFreeBlockBytes,
      "long soak close should return PSRAM near its baseline");
  }

  return {
    target: caps.target,
    durationMs: elapsedMs,
    checkpoints: checkpoints,
    cpuFrequencyHz: sys.status.cpu.frequencyHz,
    frames: frames,
    batches: batches,
    callbacks: stats.callbacks,
    accepted: stats.accepted,
    droppedPoolFull: stats.droppedPoolFull,
    droppedQueueFull: stats.droppedQueueFull,
    receivedBytes: stats.receivedBytes,
    minimumInternalLargest: minimumInternalLargest,
    minimumDmaLargest: minimumDmaLargest,
    minimumPsramLargest: minimumPsramLargest,
    finalInternalLargest: after.internal.largestFreeBlockBytes,
    finalDmaLargest: after.dma.largestFreeBlockBytes,
    finalPsramLargest: after.psram === null
      ? null : after.psram.largestFreeBlockBytes
  };
});
