test("wifi_csi/camera-coexistence-hardware", function () {
  var caps = wifi.csi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var cam = null;
  var cameraFrame = null;
  var session = null;
  var csiFrame = null;
  var before;
  var after;
  var stats;
  var cameraFrames = 0;
  var csiFrames = 0;
  var i;

  test.equal(caps.supports.promiscuous, true,
    "camera coexistence requires the promiscuous CSI Build Context gate");
  gc();
  before = sys.status.memory;
  try {
    try { wifi.disconnect(); } catch (ignoredDisconnectError) {}
    cam = camera.open({
      pixelFormat: "grayscale",
      frameSize: "qvga",
      frameBuffers: 1,
      grabMode: "whenEmpty",
      bufferLocation: "psram"
    });
    session = wifi.csi.open({
      source: "promiscuous",
      channel: "current",
      conflict: "fail",
      capture: capture,
      queue: { capacity: 8, overflow: "drop-newest" }
    });

    for (i = 0; i < 20; i += 1) {
      cameraFrame = cam.capture(5000);
      test.ok(cameraFrame !== null,
        "camera should keep capturing while CSI callbacks are enabled");
      cameraFrames += 1;
      cameraFrame.close();
      cameraFrame = null;

      csiFrame = session.receive(i === 0 ? 8000 : 10);
      if (csiFrame !== null) {
        csiFrames += 1;
        csiFrame.close();
        csiFrame = null;
      }
    }
    stats = session.stats();
    test.ok(stats.callbacks > 0 && stats.callbacks >= stats.accepted,
      "CSI callbacks should remain active during camera capture");
    test.equal(stats.leasedFrames, 0,
      "camera coexistence should close every delivered CSI frame");
    test.equal(cameraFrames, 20,
      "camera should complete every coexistence capture");
  } finally {
    if (csiFrame !== null) csiFrame.close();
    if (cameraFrame !== null) cameraFrame.close();
    if (session !== null) session.close();
    if (cam !== null) cam.close();
    try { wifi.disconnect(); } catch (ignoredFinalDisconnectError) {}
  }

  gc();
  after = sys.status.memory;
  test.equal(wifi.status().radio.clients.wifiCsi, 0,
    "camera coexistence close should release the CSI radio lease");
  test.ok(after.internal.largestFreeBlockBytes + 131072 >=
    before.internal.largestFreeBlockBytes,
    "camera and CSI close should preserve a usable internal heap block");
  test.ok(after.dma.largestFreeBlockBytes + 131072 >=
    before.dma.largestFreeBlockBytes,
    "camera and CSI close should preserve a usable DMA heap block");
  if (before.psram !== null && after.psram !== null) {
    test.ok(after.psram.largestFreeBlockBytes + 262144 >=
      before.psram.largestFreeBlockBytes,
      "camera and CSI close should return PSRAM near its baseline");
  }

  return {
    target: caps.target,
    cameraFrames: cameraFrames,
    csiFrames: csiFrames,
    callbacks: stats.callbacks,
    accepted: stats.accepted,
    internalLargestDelta: after.internal.largestFreeBlockBytes -
      before.internal.largestFreeBlockBytes,
    dmaLargestDelta: after.dma.largestFreeBlockBytes -
      before.dma.largestFreeBlockBytes,
    psramLargestDelta: before.psram === null || after.psram === null
      ? null
      : after.psram.largestFreeBlockBytes - before.psram.largestFreeBlockBytes
  };
});
