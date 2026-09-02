test("load/load", function () {
  function readFileAllocation() {
    var allocations = sys.status.memory.manager.allocations;
    var bytes = 0;
    var blocks = 0;
    var index;

    for (index = 0; index < allocations.length; index += 1) {
      if (allocations[index].owner === "fs.read-file") {
        bytes += allocations[index].bytes;
        blocks += allocations[index].blocks;
      }
    }
    return { bytes: bytes, blocks: blocks };
  }

  var before = readFileAllocation();
  var after;

  globalThis.__loadFixtureHits = 0;
  globalThis.__loadFixtureValue = null;

  load("fixtures/load-helper.js");

  test.equal(globalThis.__loadFixtureHits, 1, "fixture load count");
  test.equal(globalThis.__loadFixtureValue, "fixture-ok", "fixture load value");

  load("fixtures/load-unicode.js");
  test.equal(globalThis.__loadUnicodeFixture, "中文🙂边界",
    "load should preserve UTF-8 comments and string literals");

  after = readFileAllocation();
  test.equal(after.bytes, before.bytes,
    "load should release managed script bytes");
  test.equal(after.blocks, before.blocks,
    "load should release managed script blocks");

  return { hits: globalThis.__loadFixtureHits };
});
