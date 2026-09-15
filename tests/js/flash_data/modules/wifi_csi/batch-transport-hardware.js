test("wifi_csi/batch-transport-hardware", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var caps = wifi.csi.capabilities();
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
  var workspaceFs = fs;
  var prefix = ".__csi_batch_" + sys.millis() + "_" + Math.floor(Math.random() * 0x10000000);
  var path = prefix + ".bin";
  var rpcPath = prefix + "_rpc.bin";
  var pathOwned = false;
  var rpcPathOwned = false;
  var primaryError = null;
  var cleanupFailure = null;
  var sourceBytes;
  var written;
  var usbWritten = null;
  var usbStatus = "not-run: requires dedicated USB ownership";
  var rpcWritten;

  function closeOwned(value) {
    try { if (value !== null) value.close(); }
    catch (closeError) { if (cleanupFailure === null) cleanupFailure = closeError; }
  }
  function removeOwned(name, owned) {
    try { if (owned && workspaceFs.exists(name)) workspaceFs.remove(name); }
    catch (removeError) { if (cleanupFailure === null) cleanupFailure = removeError; }
  }

  test.equal(caps.supports.promiscuous, true,
    "batch hardware capture requires the promiscuous Build Context gate");
  test.equal(workspaceFs.exists(path), false, "test file must not replace workspace data");
  test.equal(workspaceFs.exists(rpcPath), false, "RPC test file must not replace workspace data");
  try {
    try { wifi.disconnect(); } catch (ignoredDisconnectError) {}
    wifi.connect(cfg.wifiSsid, {
      password: cfg.wifiPassword,
      timeoutMs: 15000
    });
    session = wifi.csi.open({
      source: { mode: "promiscuous", channel: "current" },
      capture: capture,
      buffering: { queueCapacity: 8, overflow: "drop-newest" }
    });
    status = session.status();
    wifi.scan({
      channel: status.effective.channel,
      showHidden: true,
      mode: "active",
      activeMinMs: 250,
      activeMaxMs: 250,
      timeoutMs: 3000
    });
    batch = session.receiveBatch({ maximumFrames: 8, timeoutMs: 8000 });
    test.ok(batch !== null && batch.frameCount >= 1,
      "batch capture should retain at least one frame");
    test.ok(batch.info(0).layout.byteLength > 0,
      "batch metadata should describe a non-empty payload");
    source = batch.source({ format: "esp32qjs-csi/1" });
    sourceBytes = source.byteLength;
    test.ok(sourceBytes > batch.info(0).layout.byteLength,
      "the batch protocol should include header and normalized metadata");
    pathOwned = true;
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

    // The Agent executor can own this same physical USB stream. Only a harness
    // explicitly given a dedicated idle USB transport may inject raw bytes.
    if (test.directUsbSerialIdle === true && sys.info.features.usbSerial) {
      usbSource = batch.source({ format: "esp32qjs-csi/1" });
      serial = usbSerial.open({ mode: "binary", chunkBytes: 4096 });
      usbWritten = serial.send(usbSource);
      usbSource.close();
      usbSource = null;
      serial.close();
      serial = null;
      test.equal(usbWritten, sourceBytes,
        "USB Serial should consume the complete CSI batch source");
      usbStatus = "passed";
    }

    codec = rpc.createCodec({
      fields: ["data"],
      dynamicFields: [],
      streamDirectory: "/"
    });
    rpcInputSource = batch.source({ format: "esp32qjs-csi/1" });
    rpcOutputSource = codec.encode(1, 1, 0, { data: rpcInputSource });
    test.ok(rpcOutputSource instanceof _ByteSpanSource,
      "RPC should frame a final CSI ByteSpanSource without flattening it");
    rpcPathOwned = true;
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
  } catch (error) {
    primaryError = error;
    throw error;
  } finally {
    closeOwned(stream);
    closeOwned(source);
    closeOwned(serial);
    closeOwned(usbSource);
    closeOwned(rpcOutputSource);
    closeOwned(rpcInputSource);
    closeOwned(codec);
    closeOwned(batch);
    closeOwned(session);
    try { wifi.disconnect(); }
    catch (disconnectError) { if (cleanupFailure === null) cleanupFailure = disconnectError; }
    removeOwned(path, pathOwned);
    removeOwned(rpcPath, rpcPathOwned);
    if (cleanupFailure !== null) {
      if (primaryError !== null) primaryError.cleanupError = String(cleanupFailure);
      else throw cleanupFailure;
    }
  }

  return {
    target: caps.target,
    fileBytes: written,
    usbBytes: usbWritten,
    usbStatus: usbStatus,
    rpcBytes: rpcWritten
  };
});
