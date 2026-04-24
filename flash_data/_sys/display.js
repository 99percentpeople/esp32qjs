(function (global) {
  if (global.display && global.display.__loaded) {
    return;
  }

  load("_sys/display/core.js");
  load("_sys/display/drivers/ssd1306.js");
  load("_sys/display/drivers/st7789.js");
  load("_sys/display/index.js");
})(globalThis);
