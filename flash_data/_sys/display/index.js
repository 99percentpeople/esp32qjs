(function (global) {
  var system = global.__displaySystem;
  var display = global.display;

  if (!system || system.indexLoaded) {
    return;
  }

  display.__loaded = true;
  system.indexLoaded = true;
})(globalThis);
