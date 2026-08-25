test("timers/future-gc-pressure-setup", function () {
  var before = sys.status.memory.internal;
  var future = null;
  var i;

  for (i = 0; i < 8; i++) {
    future = Future.call(function () {
      return { future: future };
    }, this, []);
    future.wait(1000);
  }
  future = null;
  globalThis.__futureGcPressure = {
    freeBytes: before.freeBytes,
    allocatedBlocks: before.allocatedBlocks,
    count: 8,
  };

  return { futures: globalThis.__futureGcPressure.count };
});
