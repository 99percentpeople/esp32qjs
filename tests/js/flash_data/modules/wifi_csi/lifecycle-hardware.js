test("wifi_csi/lifecycle-hardware", function () {
  var caps = wifiCsi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var options = {
    source: "associated",
    channel: "current",
    conflict: "fail",
    capture: capture,
    queue: { capacity: 2, overflow: "drop-newest" }
  };
  var session = null;
  var stats;
  var before;
  var after;
  var i;

  function openAndClose() {
    session = wifiCsi.open(options);
    session.close();
    stats = session.stats();
    test.equal(stats.leasedFrames, 0,
      "closed CSI sessions should release every frame lease");
    test.equal(wifi.status().radio.clients.wifiCsi, 0,
      "closed CSI sessions should release the radio lease");
    session = null;
  }

  try {
    try { wifi.disconnect(); } catch (ignoredDisconnectError) {}
    for (i = 0; i < 10; i += 1) openAndClose();
    gc();
    before = sys.status.memory;
    for (i = 0; i < 500; i += 1) {
      openAndClose();
      if ((i + 1) % 25 === 0) gc();
    }
    gc();
    after = sys.status.memory;

    test.ok(after.internal.freeBytes + 8192 >= before.internal.freeBytes,
      "500 CSI close/reopen cycles should return internal heap to baseline");
    test.ok(after.internal.largestFreeBlockBytes + 32768 >=
      before.internal.largestFreeBlockBytes,
      "500 CSI close/reopen cycles should preserve a usable internal block");
    test.ok(after.dma.largestFreeBlockBytes + 32768 >=
      before.dma.largestFreeBlockBytes,
      "500 CSI close/reopen cycles should preserve a usable DMA block");
    if (before.psram !== null && after.psram !== null) {
      test.ok(after.psram.largestFreeBlockBytes + 131072 >=
        before.psram.largestFreeBlockBytes,
        "500 CSI close/reopen cycles should not continuously consume PSRAM");
    }
  } finally {
    if (session !== null) session.close();
    try { wifi.disconnect(); } catch (ignoredFinalDisconnectError) {}
  }

  return {
    target: caps.target,
    cycles: 500,
    internalFreeDelta: after.internal.freeBytes - before.internal.freeBytes,
    internalLargestDelta: after.internal.largestFreeBlockBytes -
      before.internal.largestFreeBlockBytes,
    dmaLargestDelta: after.dma.largestFreeBlockBytes -
      before.dma.largestFreeBlockBytes,
    psramLargestDelta: before.psram === null || after.psram === null
      ? null
      : after.psram.largestFreeBlockBytes - before.psram.largestFreeBlockBytes
  };
});
