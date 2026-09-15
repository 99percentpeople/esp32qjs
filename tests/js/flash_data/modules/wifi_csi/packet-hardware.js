test("wifi_csi/packet-hardware", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var caps = wifi.csi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var session = null;
  var frame = null;
  var packet = null;
  var copy = null;
  var source = null;
  var client = null;
  var response = null;
  var stream = null;
  var volume = fs;
  var path = "csi-packet-" + sys.millis() + ".bin";
  var outputOwned = false;
  var requestText = "GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";
  var request = [];
  var info;
  var stats;
  var wireBytes;
  var i;
  var stage = "connect";
  test.equal(volume.exists(path), false, "packet output must not replace user data");
  try {
    wifi.connect(cfg.wifiSsid, { password: cfg.wifiPassword, timeoutMs: 15000 });
    stage = "capture";
    session = wifi.csi.open({ source: { mode: "associated" }, capture: capture,
      packet: { content: "full", snapLength: 256, required: true },
      buffering: { poolCapacity: 4, queueCapacity: 4 } });
    client = socket.openTCP({ localPort: 0 });
    test.ok(client.connect("example.com", 80, { timeoutMs: 10000 }), "traffic connection succeeds");
    for (i = 0; i < requestText.length; i += 1) request.push(requestText.charCodeAt(i));
    test.ok(client.send(request, 5000) > 0, "traffic request submitted");
    response = client.recv(512, 5000);
    test.ok(response !== null && response.byteLength > 0, "traffic reaches associated receiver");
    response.close(); response = null;
    client.close(); client = null;
    stage = "receive-packet";
    frame = session.receive(8000);
    test.ok(frame !== null, "same native CSI observation must provide a proven packet prefix");
    info = frame.info;
    test.ok(info.packet !== null && info.packet.capture.parseValid, "captured MAC header parses");
    packet = frame.packetBytes();
    copy = frame.copyPacketBytes();
    test.ok(packet !== null && copy !== null, "packet views expose the same captured prefix");
    test.equal(packet.byteLength, info.packet.capture.capturedLength, "metadata matches retained packet bytes");
    test.ok(packet.byteLength <= info.packet.capture.readableLength && packet.byteLength <= 256,
      "packet stays within actual copy receipt and requested snap length");
    source = frame.source({ format: "esp32qjs-csi/1" });
    wireBytes = source.byteLength;
    stats = session.stats();
    frame.close(); frame = null;
    session.close(); session = null;
    gc();
    for (i = 0; i < packet.byteLength; i += 1)
      test.equal(packet.getUint8(i), copy.getUint8(i), "retained packet survives Frame/Session close and GC");
    packet.close(); packet = null;
    copy.close(); copy = null;
    stage = "wire";
    outputOwned = true;
    stream = volume.open(path, "wb");
    test.equal(stream.write(source), wireBytes, "combined CSI/packet Source survives all other owners");
    stream.close(); stream = null;
    source.close(); source = null;
    test.equal(wifi.diagnostics.snapshot().csi.reservedSlots, 0, "last packet Source releases the pool");
    return { target: caps.target, csiBytes: info.layout.byteLength,
      packetBytes: info.packet.capture.capturedLength, wireBytes: wireBytes, stats: stats };
  } catch (error) {
    error.testStage = stage;
    if (session !== null) error.details = { stats: session.stats(), status: session.status() };
    throw error;
  } finally {
    if (stream !== null) stream.close();
    if (source !== null) source.close();
    if (copy !== null) copy.close();
    if (packet !== null) packet.close();
    if (frame !== null) frame.close();
    if (response !== null) response.close();
    if (client !== null) client.close();
    if (session !== null) session.close();
    if (outputOwned && volume.exists(path)) volume.remove(path);
    wifi.disconnect();
  }
});
