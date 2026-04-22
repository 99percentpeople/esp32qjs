test("load/load", function () {
  globalThis.__loadFixtureHits = 0;
  globalThis.__loadFixtureValue = null;

  load("fixtures/load-helper.js");

  test.equal(globalThis.__loadFixtureHits, 1, "fixture load count");
  test.equal(globalThis.__loadFixtureValue, "fixture-ok", "fixture load value");

  return { hits: globalThis.__loadFixtureHits };
});
