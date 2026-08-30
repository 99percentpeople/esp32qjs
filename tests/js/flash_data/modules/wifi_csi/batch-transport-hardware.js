test("wifi_csi/batch-transport-hardware", function () {
  var caps = wifiCsi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var session = null;
  var batch = null;
  var source = null;
  var stream = null;
  var path = "wifi-csi-batch.bin";
  var sourceInfo;
  var written;

  test.equal(caps.supports.promiscuous, true,
    "batch hardware capture requires the promiscuous Build Context gate");
  try {
    try { wifi.disconnect(); } catch (ignoredDisconnectError) {}
    session = wifiCsi.open({
      source: "promiscuous",
      channel: "current",
      conflict: "fail",
      capture: capture,
      queue: { capacity: 8, overflow: "drop-newest" }
    });
    batch = session.receiveBatch(8, 8000);
    test.ok(batch !== null && batch.frameCount >= 1,
      "batch capture should retain at least one frame");
    test.ok(batch.info(0).layout.byteLength > 0,
      "batch metadata should describe a non-empty payload");
    source = batch.source({ format: "esp32qjs-csi/1" });
    sourceInfo = rpc.sourceInfo(source);
    test.ok(sourceInfo.size > batch.info(0).layout.byteLength,
      "the batch protocol should include header and normalized metadata");
    stream = fs.open(path, "w");
    written = stream.write(source);
    test.equal(written, sourceInfo.size,
      "file transport should consume the complete scatter/gather source");
    stream.close();
    stream = null;
    source.close();
    source = null;
    test.equal(fs.stat(path).size, written,
      "persisted esp32qjs-csi/1 bytes should match the source length");
  } finally {
    if (stream !== null) stream.close();
    if (source !== null) source.close();
    if (batch !== null) batch.close();
    if (session !== null) session.close();
    try { if (fs.exists(path)) fs.remove(path); } catch (ignoredRemoveError) {}
  }

  return { target: caps.target, bytes: written };
});
