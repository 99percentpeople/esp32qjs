__esp32qjsTest.run("load/load", function () {
  globalThis.__loadFixtureHits = 0;
  globalThis.__loadFixtureValue = null;

  load("fixtures/load-helper.js");

  __esp32qjsTest.equal(globalThis.__loadFixtureHits, 1, "fixture load count");
  __esp32qjsTest.equal(globalThis.__loadFixtureValue, "fixture-ok", "fixture load value");

  return { hits: globalThis.__loadFixtureHits };
});
