load("_sys/display/st7789.js");

(function (global) {
  if (global.pngScrollDemo && typeof global.pngScrollDemo.stop === "function") {
    global.pngScrollDemo.stop();
  }

  var CONFIG = global.pngScrollConfig || {};
  var PNG_PATH = CONFIG.path || "demo/assets/logo.png";
  var REPORT_MS = CONFIG.reportMs || 1000;
  var FRAME_DELAY_MS = CONFIG.frameDelayMs == null ? 0 : CONFIG.frameDelayMs;
  var STEP = CONFIG.step || 2;
  var AUTO_START = CONFIG.autoStart !== false;

  function option(name, fallback) {
    return Object.prototype.hasOwnProperty.call(CONFIG, name) ? CONFIG[name] : fallback;
  }

  function mergeOptions(defaults, overrides) {
    var result = {};
    var key;

    defaults = defaults || {};
    overrides = overrides || {};
    for (key in defaults) {
      if (Object.prototype.hasOwnProperty.call(defaults, key)) {
        result[key] = defaults[key];
      }
    }
    for (key in overrides) {
      if (Object.prototype.hasOwnProperty.call(overrides, key)) {
        result[key] = overrides[key];
      }
    }
    return result;
  }

  function makeDisplayOptions() {
    var base = CONFIG.display || {};
    var transport = base.transport || {};
    var transferBytes = option("maxTransferSize", 16384);
    var options = {
      transport: {
        busOptions: mergeOptions({
          host: option("spi", spi.DEFAULT_HOST),
          sclk: option("sclk", 7),
          mosi: option("mosi", 9),
          miso: option("miso", -1),
          maxTransferSize: transferBytes
        }, transport.busOptions),
        deviceOptions: mergeOptions({
          cs: option("cs", 44),
          mode: 0,
          freqHz: option("speed", 40000000),
          queueSize: option("queueSize", 2)
        }, transport.deviceOptions),
        pins: mergeOptions({
          dc: option("dc", 8),
          reset: option("reset", -1),
          backlight: option("backlight", -1)
        }, transport.pins)
      },
      driver: mergeOptions({
        width: option("width", 240),
        height: option("height", 240),
        rotation: option("rotation", 0)
      }, base.driver),
      surface: mergeOptions({
        storage: "dma",
        fallbackStorage: "auto",
        chunkBytes: option("chunkBytes", transferBytes)
      }, base.surface),
      display: mergeOptions({ metrics: true }, base.display)
    };

    if (Object.prototype.hasOwnProperty.call(transport, "bus")) {
      options.transport.bus = transport.bus;
    }
    if (Object.prototype.hasOwnProperty.call(transport, "device")) {
      options.transport.device = transport.device;
    }
    if (Object.prototype.hasOwnProperty.call(transport, "backlightActive")) {
      options.transport.backlightActive = transport.backlightActive;
    }
    return options;
  }

  function openDisplay() {
    var options = makeDisplayOptions();
    var transport = display.transports.create("spi4wire", options.transport);
    var driverOptions = mergeOptions(options.driver, { transport: transport });
    var driver = display.drivers.create("st7789", driverOptions);

    return display.open(driver, {
      surface: options.surface,
      present: options.display.present,
      metrics: options.display.metrics === true
    });
  }

  function byteAt(data, index) {
    return data.charCodeAt(index) & 0xff;
  }

  function readU32(data, index) {
    return (((byteAt(data, index) << 24) >>> 0) |
      (byteAt(data, index + 1) << 16) |
      (byteAt(data, index + 2) << 8) |
      byteAt(data, index + 3)) >>> 0;
  }

  function readU16Le(data, index) {
    return byteAt(data, index) | (byteAt(data, index + 1) << 8);
  }

  function chunkType(data, index) {
    return String.fromCharCode(
      byteAt(data, index),
      byteAt(data, index + 1),
      byteAt(data, index + 2),
      byteAt(data, index + 3)
    );
  }

  function checkPngSignature(data) {
    var expected = [137, 80, 78, 71, 13, 10, 26, 10];
    if (!data || data.length < expected.length) {
      return false;
    }
    for (var i = 0; i < expected.length; i++) {
      if (byteAt(data, i) !== expected[i]) {
        return false;
      }
    }
    return true;
  }

  function inflateStoredBlocks(data) {
    if (data.length < 6) {
      throw new Error("PNG IDAT zlib stream is truncated");
    }

    var pos = 2;
    var out = "";
    while (pos < data.length - 4) {
      var header = byteAt(data, pos++);
      var finalBlock = header & 1;
      var blockType = (header >> 1) & 3;
      if (blockType !== 0) {
        throw new Error("PNG decoder only supports stored deflate blocks");
      }
      if (pos + 4 > data.length) {
        throw new Error("PNG stored block header is truncated");
      }
      var len = readU16Le(data, pos);
      var nlen = readU16Le(data, pos + 2);
      pos += 4;
      if (((len ^ 0xffff) & 0xffff) !== nlen) {
        throw new Error("PNG stored block length check failed");
      }
      if (pos + len > data.length) {
        throw new Error("PNG stored block payload is truncated");
      }
      out += data.slice(pos, pos + len);
      pos += len;
      if (finalBlock) {
        return out;
      }
    }
    throw new Error("PNG stored deflate stream has no final block");
  }

  function readBinary(path) {
    if (typeof fs !== "object" || !fs || typeof fs.open !== "function") {
      throw new Error("png_scroll demo requires fs.open()");
    }

    var stream = fs.open(path, "rb");
    var out = "";
    var chunk;
    try {
      while ((chunk = stream.read(512)) !== null) {
        if (typeof chunk === "string") {
          out += chunk;
        } else {
          if (!chunk || typeof chunk.toArray !== "function") {
            throw new Error("png_scroll demo expected a string or ByteView chunk");
          }
          var bytes = chunk.toArray();
          out += String.fromCharCode.apply(String, bytes);
        }
      }
    } finally {
      stream.close();
    }
    return out;
  }

  function loadPngData() {
    if (typeof CONFIG.pngData === "string") {
      return {
        data: CONFIG.pngData,
        label: "config.pngData",
      };
    }

    return {
      data: readBinary(PNG_PATH),
      label: PNG_PATH,
    };
  }

  function mergeRowRuns(rows) {
    var rects = [];
    var active = {};

    for (var y = 0; y < rows.length; y++) {
      var next = {};
      var row = rows[y];
      for (var i = 0; i < row.length; i++) {
        var run = row[i];
        var key = run.x + "," + run.width + "," + run.color;
        var rect = active[key];
        if (rect) {
          rect.height++;
        } else {
          rect = {
            x: run.x,
            y: y,
            width: run.width,
            height: 1,
            color: run.color,
          };
        }
        next[key] = rect;
      }

      for (var activeKey in active) {
        if (Object.prototype.hasOwnProperty.call(active, activeKey) &&
            !Object.prototype.hasOwnProperty.call(next, activeKey)) {
          rects.push(active[activeKey]);
        }
      }
      active = next;
    }

    for (var key in active) {
      if (Object.prototype.hasOwnProperty.call(active, key)) {
        rects.push(active[key]);
      }
    }
    return rects;
  }

  function readPngData(data, label) {
    if (typeof data !== "string") {
      throw new Error("png_scroll demo expects binary string PNG data");
    }
    if (!checkPngSignature(data)) {
      throw new Error(label + " is not a PNG file");
    }

    var pos = 8;
    var width = 0;
    var height = 0;
    var bitDepth = 0;
    var colorType = 0;
    var idat = "";

    while (pos + 12 <= data.length) {
      var length = readU32(data, pos);
      var type = chunkType(data, pos + 4);
      var chunkStart = pos + 8;
      var next = chunkStart + length + 4;
      if (next > data.length) {
        throw new Error("PNG chunk " + type + " is truncated");
      }

      if (type === "IHDR") {
        width = readU32(data, chunkStart);
        height = readU32(data, chunkStart + 4);
        bitDepth = byteAt(data, chunkStart + 8);
        colorType = byteAt(data, chunkStart + 9);
      } else if (type === "IDAT") {
        idat += data.slice(chunkStart, chunkStart + length);
      } else if (type === "IEND") {
        break;
      }

      pos = next;
    }

    if (!width || !height || bitDepth !== 8 || (colorType !== 2 && colorType !== 6)) {
      throw new Error("PNG must be 8-bit RGB or RGBA");
    }

    var raw = inflateStoredBlocks(idat);
    var bytesPerPixel = colorType === 6 ? 4 : 3;
    var rowBytes = width * bytesPerPixel;
    var expectedRaw = height * (rowBytes + 1);
    if (raw.length < expectedRaw) {
      throw new Error("PNG image data is truncated");
    }

    var rows = [];
    var offset = 0;
    for (var y = 0; y < height; y++) {
      var filter = byteAt(raw, offset++);
      if (filter !== 0) {
        throw new Error("PNG decoder only supports filter type 0");
      }
      var row = [];
      var runColor = null;
      var runX = 0;
      var runWidth = 0;
      for (var x = 0; x < width; x++) {
        var r = byteAt(raw, offset);
        var g = byteAt(raw, offset + 1);
        var b = byteAt(raw, offset + 2);
        var a = colorType === 6 ? byteAt(raw, offset + 3) : 255;
        var color = a < 128 ? null : display.rgb565(r, g, b);
        if (color !== runColor) {
          if (runColor !== null) {
            row.push({ x: runX, width: runWidth, color: runColor });
          }
          runColor = color;
          runX = x;
          runWidth = 1;
        } else {
          runWidth++;
        }
        offset += bytesPerPixel;
      }
      if (runColor !== null) {
        row.push({ x: runX, width: runWidth, color: runColor });
      }
      rows.push(row);
    }

    return {
      path: label,
      width: width,
      height: height,
      rows: rows,
      rects: mergeRowRuns(rows),
    };
  }

  function nowUs() {
    return typeof sys === "object" && sys && typeof sys.micros === "function"
      ? sys.micros()
      : Date.now() * 1000;
  }

  function round2(value) {
    return Math.round(value * 100) / 100;
  }

  function clonePerf(perf) {
    if (!perf) {
      return null;
    }
    var out = {};
    for (var key in perf) {
      if (Object.prototype.hasOwnProperty.call(perf, key)) {
        out[key] = perf[key];
      }
    }
    return out;
  }

  function readPerf(surface) {
    return surface && typeof surface.stats === "function"
      ? clonePerf(surface.stats())
      : null;
  }

  var reusedScreen = CONFIG.screen ||
    (CONFIG.reuseUiDemoScreen !== false &&
      global.uiDemo &&
      global.uiDemo.screen);
  var screen = reusedScreen || openDisplay();
  var pngSource = loadPngData();
  var image = readPngData(pngSource.data, pngSource.label);
  pngSource.data = null;
  var width = screen.width || option("width", 240);
  var height = screen.height || option("height", 240);
  var gap = option("gap", 32);
  var logoY = option("logoY", Math.max(24, ((height - image.height) / 2) | 0));
  var colors = {
    background: display.rgb565(5, 9, 14),
    hud: display.rgb565(14, 22, 31),
    hudLine: display.rgb565(34, 214, 255),
    text: display.rgb565(238, 246, 255),
    accent: display.rgb565(248, 213, 82),
  };
  var frame = 0;
  var offsetX = 0;
  var running = false;
  var timer = null;
  var windowStartUs = 0;
  var windowFrames = 0;
  var latest = null;
  var visibleFps = "-- FPS";

  function renderPngAt(dx, dy) {
    var fills = 0;
    for (var i = 0; i < image.rects.length; i++) {
      var rect = image.rects[i];
      var x = dx + rect.x;
      var y = dy + rect.y;
      var w = rect.width;
      var h = rect.height;

      if (x >= width || x + w <= 0 || y >= height || y + h <= 0) {
        continue;
      }
      if (x < 0) {
        w += x;
        x = 0;
      }
      if (x + w > width) {
        w = width - x;
      }
      if (y < 0) {
        h += y;
        y = 0;
      }
      if (y + h > height) {
        h = height - y;
      }
      if (w > 0 && h > 0) {
        screen.fillRect(x, y, w, h, rect.color);
        fills++;
      }
    }
    return fills;
  }

  function drawHud() {
    screen.fillRect(0, 0, width, 22, colors.hud);
    screen.fillRect(0, 21, width, 1, colors.hudLine);
    screen.drawText(6, 6, "PNG LOGO " + visibleFps, { color: colors.text, spacing: 0 });
    screen.drawText(width - 58, 6, "F" + frame, { color: colors.accent, spacing: 0 });
    return 2;
  }

  function drawScene() {
    var fills = 0;
    var span = image.width + gap;
    var x = -offsetX;
    screen.fillRect(0, 0, width, height, colors.background);
    fills++;
    while (x > -span) {
      x -= span;
    }
    while (x < width) {
      fills += renderPngAt(x, logoY);
      x += span;
    }
    fills += drawHud();
    return fills;
  }

  function drawFrame(options) {
    options = options || {};
    var startUs = nowUs();
    var fills = drawScene();
    var drawUs = nowUs() - startUs;
    var flushUs = 0;

    if (options.flush !== false) {
      var flushStartUs = nowUs();
      screen.flush();
      flushUs = nowUs() - flushStartUs;
    }

    offsetX = (offsetX + (options.step || STEP)) % (image.width + gap);
    frame++;

    var totalUs = nowUs() - startUs;
    latest = {
      frame: frame,
      frameUs: totalUs,
      drawUs: drawUs,
      flushUs: flushUs,
      fills: fills,
      offsetX: offsetX,
    };
    return latest;
  }

  function reportIfNeeded() {
    var now = nowUs();
    if (!windowStartUs) {
      windowStartUs = now;
      windowFrames = 0;
      return;
    }
    windowFrames++;
    if (now - windowStartUs >= REPORT_MS * 1000) {
      var fps = windowFrames * 1000000 / (now - windowStartUs);
      visibleFps = round2(fps) + " FPS";
      print("[png:scroll] fps=" + round2(fps) +
        " frame_ms=" + round2((latest ? latest.frameUs : 0) / 1000) +
        " draw_ms=" + round2((latest ? latest.drawUs : 0) / 1000) +
        " flush_ms=" + round2((latest ? latest.flushUs : 0) / 1000) +
        " fills=" + (latest ? latest.fills : 0));
      windowStartUs = now;
      windowFrames = 0;
    }
  }

  function tick() {
    if (!running) {
      return;
    }
    drawFrame();
    reportIfNeeded();
    timer = setTimeout(tick, FRAME_DELAY_MS);
  }

  function start() {
    if (running) {
      return global.pngScrollDemo;
    }
    running = true;
    windowStartUs = nowUs();
    windowFrames = 0;
    timer = setTimeout(tick, 0);
    return global.pngScrollDemo;
  }

  function stop() {
    running = false;
    if (timer !== null) {
      clearTimeout(timer);
      timer = null;
    }
    return global.pngScrollDemo;
  }

  function resetPerf() {
    if (screen && typeof screen.resetStats === "function") {
      screen.resetStats();
    }
  }

  function benchmark(frames, options) {
    stop();
    frames = frames || 60;
    options = options || {};
    resetPerf();
    var startUs = nowUs();
    var last = null;
    for (var i = 0; i < frames; i++) {
      last = drawFrame(options);
    }
    var totalUs = nowUs() - startUs;
    return {
      frames: frames,
      totalUs: totalUs,
      avgFrameUs: totalUs / frames,
      fps: frames * 1000000 / totalUs,
      lastFrameUs: last ? last.frameUs : 0,
      lastDrawUs: last ? last.drawUs : 0,
      lastFlushUs: last ? last.flushUs : 0,
      fills: last ? last.fills : 0,
      image: {
        path: image.path,
        width: image.width,
        height: image.height,
      },
      perf: readPerf(screen),
    };
  }

  global.pngScrollDemo = {
    screen: screen,
    image: image,
    start: start,
    stop: stop,
    drawFrame: drawFrame,
    benchmark: benchmark,
    stats: function () {
      return latest;
    },
  };

  print("[png:scroll] loaded " + PNG_PATH + " " + image.width + "x" + image.height +
    (reusedScreen ? " using existing screen" : " using new screen"));
  print("[png:scroll] commands: pngScrollDemo.benchmark(60), pngScrollDemo.stop(), pngScrollDemo.start()");

  if (AUTO_START) {
    start();
  }
})(globalThis);
