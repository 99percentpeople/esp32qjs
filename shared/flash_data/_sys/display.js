(function (global) {
  var system = global.__displaySystemV2;

  if (system && system.coreLoaded && global.display && global.display.__loaded) {
    return;
  }

  load("_sys/display/core/namespace.js");
  load("_sys/display/core/fonts.js");
  load("_sys/display/core/surface.js");
  load("_sys/display/core/present.js");
  load("_sys/display/core/display.js");

  system = global.__displaySystemV2;
  system.coreLoaded = true;
  global.display.__loaded = true;
})(globalThis);
