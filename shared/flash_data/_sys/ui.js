(function (global) {
  if (global.ui && global.ui.__loaded) {
    return;
  }

  load("_sys/ui/core.js");
})(globalThis);