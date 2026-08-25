test("socket/network", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var status = wifi.status();
  var timeOptions = {
    servers: ["pool.ntp.org", "time.cloudflare.com"],
    timeoutMs: 15000
  };
  var before;
  var after;
  var i;

  if (!status.connected) {
    status = Future.call(wifi.connect, wifi,
      [cfg.wifiSsid, cfg.wifiPassword, 15000]).wait(20000);
  }
  test.ok(status.connected, "wifi should be connected before raw TLS");
  test.equal(wifi.syncTime(timeOptions).synchronized, true,
    "time should be synchronized before raw TLS");

  function connectAndClose() {
    var id = socket.open("tcp", { tls: true });
    try {
      test.ok(socket.tcp.connect(id, "example.com", 443, 15000),
        "raw TLS should connect to a public CA host");
    } finally {
      socket.close(id);
    }
  }

  connectAndClose();
  gc();
  before = sys.status.memory;
  for (i = 0; i < 5; i += 1) connectAndClose();
  gc();
  after = sys.status.memory;

  test.ok(after.internal.largestFreeBlockBytes > 0,
    "internal heap should retain a usable contiguous block");
  test.ok(after.dma.largestFreeBlockBytes > 0,
    "DMA heap should retain a usable contiguous block");
  if (before.psram !== null && after.psram !== null) {
    test.ok(after.psram.largestFreeBlockBytes + 131072 >=
      before.psram.largestFreeBlockBytes,
      "closed TLS sessions should not continuously consume PSRAM");
  }

  wifi.disconnect();
  return {
    internalLargest: after.internal.largestFreeBlockBytes,
    dmaLargest: after.dma.largestFreeBlockBytes,
    psramLargest: after.psram === null
      ? null : after.psram.largestFreeBlockBytes
  };
});
