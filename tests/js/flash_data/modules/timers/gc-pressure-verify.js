test("timers/future-gc-pressure-verify", function () {
  var probe = globalThis.__futureGcPressure;
  var after = sys.status.memory.internal;

  test.ok(probe && probe.count === 8,
    "Future GC pressure setup should run in the same runtime generation");
  test.ok(after.allocatedBlocks <= probe.allocatedBlocks + 2,
    "self-referencing Future handles should be finalized without a fixed batch or explicit gc");
  test.ok(after.freeBytes >= probe.freeBytes - 256,
    "self-referencing Future handles should not retain internal heap across evaluations");
  delete globalThis.__futureGcPressure;

  return {
    allocatedBlockDelta: after.allocatedBlocks - probe.allocatedBlocks,
    freeByteDelta: after.freeBytes - probe.freeBytes,
  };
});
