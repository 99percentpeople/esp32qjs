test("uart/loopback", function () {
  var overrides = test.config().uartLoopback || {};
  var options = {
    port: overrides.port === undefined ? uart.DEFAULT_PORT : overrides.port,
    tx: overrides.tx === undefined ? uart.DEFAULT_TX : overrides.tx,
    rx: overrides.rx === undefined ? uart.DEFAULT_RX : overrides.rx,
    baud: overrides.baud === undefined ? uart.DEFAULT_BAUD : overrides.baud,
    timeoutMs: overrides.timeoutMs === undefined ? 500 : overrides.timeoutMs,
  };
  var port;
  var pattern = [0x13, 0x37, 0x42, 0x99];
  var chunks = [[0xaa, 0x55], [0x00, 0xff, 0x7e]];

  function assertBytesEqual(actual, expected, message) {
    var i;
    var bytes;

    test.ok(actual instanceof _ByteView, message + " should return ByteView");
    try {
      bytes = actual.toArray();
      test.equal(bytes.length, expected.length, message + " length");
      for (i = 0; i < expected.length; i++) {
        test.equal(bytes[i], expected[i], message + " byte " + i);
      }
    } finally {
      actual.close();
    }
  }

  if (options.tx < 0 || options.rx < 0) {
    test.skip("UART loopback requires DEFAULT_TX/DEFAULT_RX or testConfig.uartLoopback overrides");
  }

  port = uart.open(options);
  try {
    port.clearRx();
    test.equal(port.write(pattern), pattern.length, "loopback write should report byte count");
    test.equal(port.flush(500), true, "loopback flush should succeed");
    sleep(20);
    assertBytesEqual(port.read(pattern.length, 500), pattern, "loopback read should echo TX on RX");

    port.clearRx();
    var stats = port.writeChunks(chunks);
    test.equal(stats.chunks, 2, "loopback writeChunks should report chunks");
    test.equal(stats.bytes, 5, "loopback writeChunks should report byte count");
    test.equal(port.flush(500), true, "loopback writeChunks flush should succeed");
    sleep(20);
    assertBytesEqual(port.read(5, 500), [0xaa, 0x55, 0x00, 0xff, 0x7e], "loopback writeChunks should echo bytes");
  } finally {
    port.close();
  }

  return {
    port: options.port,
    tx: options.tx,
    rx: options.rx,
  };
});
