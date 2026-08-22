test("rmt/offline", function () {
  var capabilities = rmt.capabilities();
  var symbols = rmt.createSymbols(2);
  var first;
  var capacityError = "";
  var durationError = "";
  var fullError = "";
  var indexError = "";
  var resolutionError = "";

  test.equal(capabilities.rx, true, "RMT RX should be supported");
  test.equal(capabilities.tx, true, "RMT TX should be supported");
  test.ok(typeof capabilities.dma === "boolean",
    "RMT DMA support should be explicit per target");
  test.equal(capabilities.maxSymbols, 4096,
    "logical symbol buffers should be bounded");
  test.equal(capabilities.maxDurationTicks, 32767,
    "durations should match the 15-bit hardware field");
  test.equal(capabilities.finiteLoops, true,
    "transmit loops should always be finite");

  test.equal(symbols.capacity, 2, "capacity should be retained natively");
  test.equal(symbols.length, 0, "new symbol buffers should be empty");
  test.equal(symbols.push(10, 1, 20, 0), 1,
    "push should return the new logical length");
  first = symbols.get(0);
  test.equal(first.duration0Ticks, 10, "get should expose duration0");
  test.equal(first.level0, true, "get should expose level0");
  test.equal(first.duration1Ticks, 20, "get should expose duration1");
  test.equal(first.level1, false, "get should expose level1");
  test.equal(symbols.set(0, 30, false, 40, true), true,
    "set should replace an existing symbol");
  first = symbols.get(0);
  test.equal(first.duration0Ticks, 30, "set should update duration0");
  test.equal(first.level1, true, "set should update level1");
  symbols.push(1, 0, 1, 1);

  try {
    symbols.push(1, 0, 1, 0);
  } catch (fullException) {
    fullError = String(fullException);
  }
  test.ok(fullError.indexOf("capacity is exhausted") >= 0,
    "push should reject a full logical buffer");

  try {
    symbols.set(2, 1, 0, 1, 0);
  } catch (indexException) {
    indexError = String(indexException);
  }
  test.ok(indexError.indexOf("existing index") >= 0,
    "set should reject an index outside the logical length");

  symbols.clear();
  test.equal(symbols.length, 0, "clear should reset logical length");
  try {
    symbols.push(32768, 0, 1, 0);
  } catch (durationException) {
    durationError = String(durationException);
  }
  test.ok(durationError.indexOf("15-bit") >= 0,
    "durations longer than the hardware field should be rejected");

  try {
    rmt.createSymbols(0);
  } catch (capacityException) {
    capacityError = String(capacityException);
  }
  test.ok(capacityError.indexOf("1 through 4096") >= 0,
    "empty symbol buffers should be rejected");

  try {
    rmt.open({ direction: "tx", pin: 0 });
  } catch (resolutionException) {
    resolutionError = String(resolutionException);
  }
  test.ok(resolutionError.indexOf("resolutionHz") >= 0,
    "channel resolution should be explicit");

  test.equal(symbols.close(), true, "close should release the native buffer");
  test.equal(symbols.close(), true, "close should be idempotent");
  return {
    dma: capabilities.dma,
    minMemorySymbols: capabilities.minMemorySymbols
  };
});
