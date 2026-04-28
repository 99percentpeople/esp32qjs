test("uart/basic", function () {
  var info = esp32.info();
  var port;
  var status;
  var overridePort;
  var overrideStatus;
  var staleError = "";
  var writeSourceRejected = false;

  test.ok(typeof uart.DEFAULT_PORT === "number", "uart.DEFAULT_PORT should be numeric");
  test.ok(typeof uart.DEFAULT_TX === "number", "uart.DEFAULT_TX should be numeric");
  test.ok(typeof uart.DEFAULT_RX === "number", "uart.DEFAULT_RX should be numeric");
  test.ok(typeof uart.DEFAULT_BAUD === "number", "uart.DEFAULT_BAUD should be numeric");
  test.ok(typeof uart.DEFAULT_RX_BUFFER_SIZE === "number", "uart.DEFAULT_RX_BUFFER_SIZE should be numeric");
  test.ok(typeof uart.DEFAULT_TX_BUFFER_SIZE === "number", "uart.DEFAULT_TX_BUFFER_SIZE should be numeric");
  test.ok(typeof uart.DEFAULT_TIMEOUT_MS === "number", "uart.DEFAULT_TIMEOUT_MS should be numeric");
  test.ok(typeof uart.open === "function", "uart.open should exist");
  test.ok(typeof uart.write === "undefined", "uart.write should not exist on the top-level module");

  port = uart.open();
  test.ok(port && typeof port === "object", "uart.open() should return a UARTPort object");
  test.ok(typeof port.status === "function", "UARTPort.status should exist");
  test.ok(typeof port.write === "function", "UARTPort.write should exist");
  test.ok(typeof port.writeChunks === "function", "UARTPort.writeChunks should exist");
  test.ok(typeof port.writeSource === "function", "UARTPort.writeSource should exist");
  test.ok(typeof port.read === "function", "UARTPort.read should exist");
  test.ok(typeof port.available === "function", "UARTPort.available should exist");
  test.ok(typeof port.flush === "function", "UARTPort.flush should exist");
  test.ok(typeof port.clearRx === "function", "UARTPort.clearRx should exist");

  status = port.status();
  test.ok(status && typeof status === "object", "UARTPort.status() should return an object");
  test.equal(status.opened, true, "UARTPort.status() should report an open port");
  test.equal(status.port, uart.DEFAULT_PORT, "UARTPort.status().port should use DEFAULT_PORT");
  test.equal(status.tx, uart.DEFAULT_TX, "UARTPort.status().tx should use DEFAULT_TX");
  test.equal(status.rx, uart.DEFAULT_RX, "UARTPort.status().rx should use DEFAULT_RX");
  test.equal(status.baud, uart.DEFAULT_BAUD, "UARTPort.status().baud should use DEFAULT_BAUD");
  test.equal(status.dataBits, 8, "UARTPort.status().dataBits should default to 8");
  test.equal(status.parity, "none", "UARTPort.status().parity should default to none");
  test.equal(status.stopBits, 1, "UARTPort.status().stopBits should default to 1");
  test.equal(status.rxBufferSize, uart.DEFAULT_RX_BUFFER_SIZE, "UARTPort.status().rxBufferSize should use DEFAULT_RX_BUFFER_SIZE");
  test.equal(status.txBufferSize, uart.DEFAULT_TX_BUFFER_SIZE, "UARTPort.status().txBufferSize should use DEFAULT_TX_BUFFER_SIZE");
  test.equal(status.timeoutMs, uart.DEFAULT_TIMEOUT_MS, "UARTPort.status().timeoutMs should use DEFAULT_TIMEOUT_MS");
  test.ok(typeof port.available() === "number", "UARTPort.available() should return a number");
  test.equal(port.write([]), 0, "UARTPort.write([]) should succeed");
  test.equal(port.writeChunks([]).chunks, 0, "UARTPort.writeChunks([]) should succeed");
  try {
    port.writeSource([]);
  } catch (writeSourceError) {
    writeSourceRejected = String(writeSourceError).indexOf("ByteSpanSource") >= 0;
  }
  test.ok(writeSourceRejected, "UARTPort.writeSource should reject non-source inputs");
  if (info.features.displayBuffer && typeof displayBuffer === "object") {
    var buffer = displayBuffer.create({ width: 1, height: 1, format: "rgb565" });
    try {
      var source = buffer.createSpanSource();
      var sourceStats = port.writeSource(source);

      test.ok(source instanceof _ByteSpanSource, "display span source should provide the generic ByteSpanSource capability");
      test.equal(sourceStats.bytes, 2, "UARTPort.writeSource should report generic source bytes");
      test.equal(sourceStats.chunks, 1, "UARTPort.writeSource should consume one source span");
    } finally {
      buffer.close();
    }
  }
  test.equal(port.read(0).length, 0, "UARTPort.read(0) should return an empty array");
  test.equal(port.flush(), true, "UARTPort.flush() should succeed");
  test.equal(port.clearRx(), true, "UARTPort.clearRx() should succeed");
  test.equal(port.close(), true, "UARTPort.close() should succeed");

  try {
    port.status();
  } catch (error) {
    staleError = String(error);
  }
  test.ok(staleError.indexOf("closed") >= 0, "closed UARTPort objects should reject further use");

  overridePort = uart.open({
    port: uart.DEFAULT_PORT,
    tx: uart.DEFAULT_TX,
    rx: -1,
    baud: 57600,
    dataBits: 7,
    parity: "even",
    stopBits: 2,
    timeoutMs: 0,
  });
  overrideStatus = overridePort.status();
  test.equal(overrideStatus.rx, -1, "uart.open({ rx }) should override DEFAULT_RX");
  test.equal(overrideStatus.baud, 57600, "uart.open({ baud }) should override DEFAULT_BAUD");
  test.equal(overrideStatus.dataBits, 7, "uart.open({ dataBits }) should override data bits");
  test.equal(overrideStatus.parity, "even", "uart.open({ parity }) should override parity");
  test.equal(overrideStatus.stopBits, 2, "uart.open({ stopBits }) should override stop bits");
  test.equal(overrideStatus.timeoutMs, 0, "uart.open({ timeoutMs }) should override timeout");
  test.equal(overridePort.close(), true, "explicit override UARTPort.close() should succeed");

  return {
    port: status.port,
    tx: status.tx,
    rx: status.rx,
    baud: status.baud,
  };
});
