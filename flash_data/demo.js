print("loaded demo.js from LittleFS");
globalThis.demoRunCount = (globalThis.demoRunCount || 0) + 1;

({
  file: "demo.js",
  runCount: globalThis.demoRunCount,
  millis: esp32.millis(),
  freeHeap: esp32.freeHeap(),
});
