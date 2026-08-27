test("timers/future-automatic-gc", function () {
  var before = sys.status.memory.internal;
  var future = null;
  var prefix = new Array(1025).join("x");
  var pressure = null;
  var after;
  var i;

  for (i = 0; i < 8; i++) {
    future = Future.call(function () {
      return { future: future };
    }, this, []);
    future.wait(1000);
  }
  future = null;

  /*
   * Repeatedly exceed the configured JavaScript heap without retaining the
   * temporary strings. MQuickJS must collect automatically in its allocation
   * path; the framework scheduler and Future implementation do not request GC.
   */
  for (i = 0; i < 2048; i++) {
    pressure = prefix + i;
  }
  pressure = null;
  after = sys.status.memory.internal;

  test.ok(after.allocatedBlocks <= before.allocatedBlocks + 2,
    "automatic MQuickJS GC should finalize unreachable Future cycles");
  test.ok(after.freeBytes >= before.freeBytes - 256,
    "automatic MQuickJS GC should release unreachable Future handles");

  return {
    allocatedBlockDelta: after.allocatedBlocks - before.allocatedBlocks,
    freeByteDelta: after.freeBytes - before.freeBytes,
  };
});
