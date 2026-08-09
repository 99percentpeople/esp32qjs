(function (global) {
  var system = global.__displaySystemV2;

  if (system && system.coreLoaded) {
    return;
  }
  load("_sys/display.js");
})(globalThis);
