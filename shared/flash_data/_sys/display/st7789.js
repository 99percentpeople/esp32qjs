(function (global) {
  var system = global.__displaySystemV2;

  if (!system || !system.coreLoaded) {
    load("_sys/display/core/namespace.js");
    load("_sys/display/core/fonts.js");
    load("_sys/display/core/surface.js");
    load("_sys/display/core/present.js");
    load("_sys/display/core/display.js");
    system = global.__displaySystemV2;
    system.coreLoaded = true;
    global.display.__loaded = true;
  }
  if (!system.spi4WireTransportLoaded) {
    load("_sys/display/transports/spi4wire.js");
  }
  if (!system.st7789DriverLoaded) {
    load("_sys/display/drivers/st7789.js");
  }
})(globalThis);
