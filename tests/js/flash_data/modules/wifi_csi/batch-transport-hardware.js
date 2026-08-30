test("wifi_csi/batch-transport-hardware", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var caps = wifiCsi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var session = null;
  var batch = null;
  var source = null;
  var serial = null;
  var usbSource = null;
  var rpcInputSource = null;
  var rpcOutputSource = null;
  var codec = null;
  var stream = null;
  var status;
  var workspaceFs = fs.volume("/workspace");
  var path = "wifi-csi-batch.bin";
  var rpcPath = "wifi-csi-rpc.bin";
  var sourceBytes;
  var written;
  var usbWritten = null;
  var rpcWritten;

  test.equal(caps.supports.promiscuous, true,
    "batch hardware capture requires the promiscuous Build Context gate");
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
      queue: { capacity: 8, overflow: "drop-newest" }
    });
    status = session.status();
    wifi.scan({
      channel: status.effective.channel,
      showHidden: true,
      passive: false,
      dwellMs: 250,
      timeoutMs: 3000
    });
    batch = session.receiveBatch(8, 8000);
    test.ok(batch !== null && batch.frameCount >= 1,
      "batch capture should retain at least one frame");
    test.ok(batch.info(0).layout.byteLength > 0,
      "batch metadata should describe a non-empty payload");
    source = batch.source({ format: "esp32qjs-csi/1" });
    sourceBytes = source.byteLength;
    test.ok(sourceBytes > batch.info(0).layout.byteLength,
      "the batch protocol should include header and normalized metadata");
    stream = workspaceFs.open(path, "wb");
    written = stream.write(source);
    test.equal(written, sourceBytes,
      "file transport should consume the complete scatter/gather source");
    stream.close();
    stream = null;
    source.close();
    source = null;
    test.equal(workspaceFs.stat(path).size, written,
      "persisted esp32qjs-csi/1 bytes should match the source length");

    if (sys.info.features.usbSerial) {
      usbSource = batch.source({ format: "esp32qjs-csi/1" });
      serial = usbSerial.open({ mode: "binary", chunkBytes: 4096 });
      usbWritten = serial.send(usbSource);
      usbSource.close();
      usbSource = null;
      serial.close();
      serial = null;
      test.equal(usbWritten, sourceBytes,
        "USB Serial should consume the complete CSI batch source");
    }

    codec = rpc.createCodec({
      fields: ["data"],
      dynamicFields: [],
      streamDirectory: "/workspace"
    });
    rpcInputSource = batch.source({ format: "esp32qjs-csi/1" });
    rpcOutputSource = codec.encode(1, 1, 0, { data: rpcInputSource });
    test.ok(rpcOutputSource instanceof _ByteSpanSource,
      "RPC should frame a final CSI ByteSpanSource without flattening it");
    stream = workspaceFs.open(rpcPath, "wb");
    rpcWritten = stream.write(rpcOutputSource);
    stream.close();
    stream = null;
    rpcOutputSource.close();
    rpcOutputSource = null;
    rpcInputSource.close();
    rpcInputSource = null;
    test.ok(rpcWritten > sourceBytes,
      "RPC framing should preserve the CSI bytes plus protocol overhead");
    test.equal(workspaceFs.stat(rpcPath).size, rpcWritten,
      "the streamed RPC envelope should be persisted completely");
  } finally {
    if (stream !== null) stream.close();
    if (source !== null) source.close();
    if (serial !== null) serial.close();
    if (usbSource !== null) usbSource.close();
    if (rpcInputSource !== null) rpcInputSource.close();
    if (rpcOutputSource !== null) rpcOutputSource.close();
    if (codec !== null) codec.close();
    if (batch !== null) batch.close();
    if (session !== null) session.close();
    try { wifi.disconnect(); } catch (ignoredFinalDisconnectError) {}
    try { if (workspaceFs.exists(path)) workspaceFs.remove(path); } catch (ignoredRemoveError) {}
    try { if (workspaceFs.exists(rpcPath)) workspaceFs.remove(rpcPath); } catch (ignoredRpcRemoveError) {}
  }

  return {
    target: caps.target,
    fileBytes: written,
    usbBytes: usbWritten,
    rpcBytes: rpcWritten
  };
});
