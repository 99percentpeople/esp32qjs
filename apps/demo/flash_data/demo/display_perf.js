load("_sys/display/wlk1501spi8p.js");

var PERF = globalThis.displayPerfConfig || {};
var owns = Object.prototype.hasOwnProperty;
var TRANSFER_BYTES = PERF.transferBytes || 32768;
var USE_BATCH = PERF.batch === true;
var HUD_H = 58;
var GRAPH_H = 36;
var MODE_SECONDS = PERF.modeSeconds || 12;
var FRAME_DELAY_MS = PERF.frameDelayMs === undefined ? 0 : PERF.frameDelayMs;
var PARTIAL_HUD_MS = PERF.partialHudMs === undefined ? 1000 : PERF.partialHudMs;
var PARTIAL_OVERLAY_MS = PERF.partialOverlayMs === undefined ? 0 : PERF.partialOverlayMs;

var COLORS = {
  bg: display.rgb565(3, 12, 26),
  panel: display.rgb565(12, 34, 58),
  panel2: display.rgb565(18, 50, 78),
  grid: display.rgb565(28, 86, 110),
  dim: display.rgb565(96, 150, 158),
  text: display.rgb565(234, 244, 255),
  yellow: display.rgb565(255, 230, 48),
  cyan: display.rgb565(0, 215, 232),
  green: display.rgb565(62, 232, 126),
  blue: display.rgb565(80, 150, 255),
  magenta: display.rgb565(236, 76, 178),
  orange: display.rgb565(255, 148, 48),
  red: display.rgb565(250, 76, 62)
};

var BAR_COLORS = [
  COLORS.cyan,
  COLORS.green,
  COLORS.yellow,
  COLORS.magenta,
  COLORS.orange,
  COLORS.blue
];

function mergeOptions(defaults, overrides) {
  var result = {};
  var key;

  defaults = defaults || {};
  overrides = overrides || {};
  for (key in defaults) {
    if (owns.call(defaults, key)) {
      result[key] = defaults[key];
    }
  }
  for (key in overrides) {
    if (owns.call(overrides, key)) {
      result[key] = overrides[key];
    }
  }
  return result;
}

function makeDisplayOptions() {
  var base = PERF.display || globalThis.displayConfig || {};
  var transport = base.transport || {};
  var options = {
    transport: {
      busOptions: mergeOptions({ maxTransferSize: TRANSFER_BYTES }, transport.busOptions),
      deviceOptions: mergeOptions({
        freqHz: PERF.freqHz || 80000000,
        queueSize: 2
      }, transport.deviceOptions),
      pins: mergeOptions({}, transport.pins)
    },
    driver: mergeOptions({}, base.driver),
    surface: mergeOptions({
      chunkBytes: TRANSFER_BYTES,
      foreground: COLORS.text,
      background: COLORS.bg
    }, base.surface),
    display: mergeOptions({ metrics: true }, base.display)
  };

  if (owns.call(transport, "bus")) {
    options.transport.bus = transport.bus;
  }
  if (owns.call(transport, "device")) {
    options.transport.device = transport.device;
  }
  if (owns.call(transport, "backlightActive")) {
    options.transport.backlightActive = transport.backlightActive;
  }
  return options;
}

var screen = display.profiles.open("wlk1501spi8p", makeDisplayOptions());
var info = esp32.info();
var width = screen.width | 0;
var height = screen.height | 0;
var bodyY = HUD_H;
var graphY = height - GRAPH_H - 4;
var graphBottom = height - 8;
var graphX = 7;
var graphW = width - 14;
var samples = Math.max(24, Math.min(96, graphW / 3 | 0));
var history = [];
var historyIndex = 0;
var frame = 0;
var timer = null;
var modeIndex = 0;
var modeStartUs = esp32.micros();
var forceFullFrame = true;
var cjk12 = null;
var cjk16 = null;
var cjk24 = null;
var lastFrameUs = 0;
var lastDrawUs = 0;
var lastFlushUs = 0;
var reportLastUs = esp32.micros();
var reportFrames = 0;
var reportFrameUs = 0;
var reportDrawUs = 0;
var reportFlushUs = 0;
var reportMaxFrameUs = 0;
var reportShapeBgUs = 0;
var reportShapeOvalUs = 0;
var reportShapePolygonUs = 0;
var reportShapeCurveUs = 0;
var reportShapeTriangleUs = 0;
var partialLastBox = null;
var partialHudUs = 0;
var partialOverlayUs = 0;
var latest = {
  fps: 0,
  frameUs: 0,
  drawUs: 0,
  flushUs: 0,
  maxFrameUs: 0,
  flushCalls: 0,
  flushTotalUs: 0,
  windowUs: 0,
  pixelUs: 0,
  dataUs: 0,
  directFlushes: 0,
  chunks: 0,
  pixels: 0,
  bytes: 0,
  heap: 0,
  batch: false,
  commandCount: 0,
  commandTextBytes: 0
};
var latestShape = {
  bgUs: 0,
  ovalUs: 0,
  polygonUs: 0,
  curveUs: 0,
  triangleUs: 0
};
var lastBatch = false;
var lastCommandStats = null;

var MODES = [
  { name: "FULL", label: "FULL SCREEN", fullFlush: true },
  { name: "PART", label: "PARTIAL RECTS", fullFlush: false },
  { name: "SHAPE", label: "VECTOR SHAPES", fullFlush: false },
  { name: "TEXT", label: "TEXT + CJK", fullFlush: false }
];

function loadCjk(size) {
  try {
    return display.loadMappedFont("_sys/fonts/droid-cjk.json", String(size));
  } catch (error) {
    print("[display:font]", "size=" + size, String(error));
  }
  return null;
}

function initHistory() {
  var i;

  history = new Array(samples);
  for (i = 0; i < samples; i += 1) {
    history[i] = 0;
  }
}

function pad(value, digits) {
  var text = String(value | 0);

  while (text.length < digits) {
    text = "0" + text;
  }
  return text;
}

function fmt1(value) {
  return ((value * 10 + 0.5) | 0) / 10;
}

function ms(us) {
  return fmt1(us / 1000);
}

function kb(bytes) {
  return ((bytes / 102.4 + 0.5) | 0) / 10;
}

function addShapeTime(name, startedUs) {
  var elapsed = esp32.micros() - startedUs;

  if (name === "bg") {
    reportShapeBgUs += elapsed;
  } else if (name === "oval") {
    reportShapeOvalUs += elapsed;
  } else if (name === "polygon") {
    reportShapePolygonUs += elapsed;
  } else if (name === "curve") {
    reportShapeCurveUs += elapsed;
  } else if (name === "triangle") {
    reportShapeTriangleUs += elapsed;
  }
}

function pingPong(value, maxValue) {
  var period;
  var phase;

  if (maxValue <= 0) {
    return 0;
  }
  period = maxValue * 2;
  phase = value % period;
  return phase > maxValue ? period - phase : phase;
}

function rect(list, x, y, w, h) {
  if (w > 0 && h > 0) {
    list.push({ x: x | 0, y: y | 0, w: w | 0, h: h | 0 });
  }
}

function unionRect(a, b) {
  var x0;
  var y0;
  var x1;
  var y1;

  if (!a) {
    return b;
  }
  if (!b) {
    return a;
  }
  x0 = a.x < b.x ? a.x : b.x;
  y0 = a.y < b.y ? a.y : b.y;
  x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
  y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
  return {
    x: x0,
    y: y0,
    w: x1 - x0,
    h: y1 - y0
  };
}

function pushSample(us) {
  history[historyIndex] = us;
  historyIndex = (historyIndex + 1) % history.length;
}

function sampleAt(index) {
  return history[(historyIndex + index) % history.length];
}

function activeMode() {
  return MODES[modeIndex];
}

function shouldUseBatch(mode) {
  if (!USE_BATCH) {
    return false;
  }
  return mode.name !== "SHAPE";
}

function setMode(index) {
  modeIndex = ((index % MODES.length) + MODES.length) % MODES.length;
  modeStartUs = esp32.micros();
  forceFullFrame = true;
  partialLastBox = null;
  partialHudUs = 0;
  partialOverlayUs = 0;
  screen.resetStats();
  print("[display:perf]", "mode=" + activeMode().name);
}

function nextMode() {
  setMode(modeIndex + 1);
}

function drawBase(surface) {
  var x;
  var y;

  surface.clear(COLORS.bg);
  surface.fillRect(0, 0, width, HUD_H, COLORS.panel);
  for (x = 0; x < width; x += 24) {
    surface.drawLine(x, bodyY, x, graphY - 3, COLORS.grid);
  }
  for (y = bodyY; y < graphY - 2; y += 20) {
    surface.drawLine(0, y, width - 1, y, COLORS.grid);
  }
}

function drawHud(surface, dirty) {
  var mode = activeMode();
  var seconds = ((esp32.micros() - modeStartUs) / 1000000) | 0;

  surface.fillRect(0, 0, width, HUD_H, COLORS.panel);
  surface.drawText(6, 4, "DISPLAY PERF " + mode.name, { color: COLORS.yellow, spacing: 0 });
  surface.drawText(width - 70, 4, "F" + pad(frame, 5), { color: COLORS.text, spacing: 0 });
  surface.drawText(6, 18,
                  "FPS " + fmt1(latest.fps) +
                  "  LAT " + ms(lastFrameUs) + "MS" +
                  "  T" + pad(seconds, 2),
                  { color: COLORS.text, spacing: 0 });
  surface.drawText(6, 30,
                  "DRAW " + ms(lastDrawUs) +
                  "  FLUSH " + ms(lastFlushUs),
                  { color: COLORS.cyan, spacing: 0 });
  surface.drawText(6, 42,
                  "AVG " + ms(latest.frameUs) +
                  "  MAX " + ms(latest.maxFrameUs) +
                  "  KB " + kb(latest.bytes),
                  { color: COLORS.green, spacing: 0 });
  rect(dirty, 0, 0, width, HUD_H);
}

function drawPartialHud(surface, dirty) {
  var seconds = ((esp32.micros() - modeStartUs) / 1000000) | 0;

  surface.fillRect(0, 18, width, HUD_H - 18, COLORS.panel);
  surface.drawText(6, 18,
                  "FPS " + fmt1(latest.fps) +
                  "  LAT " + ms(lastFrameUs) + "MS" +
                  "  T" + pad(seconds, 2),
                  { color: COLORS.text, spacing: 0 });
  surface.drawText(6, 30,
                  "DRAW " + ms(lastDrawUs) +
                  "  FLUSH " + ms(lastFlushUs),
                  { color: COLORS.cyan, spacing: 0 });
  surface.drawText(6, 42,
                  "AVG " + ms(latest.frameUs) +
                  "  MAX " + ms(latest.maxFrameUs) +
                  "  KB " + kb(latest.bytes),
                  { color: COLORS.green, spacing: 0 });
  rect(dirty, 0, 18, width, HUD_H - 18);
}

function drawFullScene(surface) {
  var i;
  var x;
  var y;
  var h;
  var x1;
  var y1;
  var x2;
  var y2;

  surface.fillRect(0, bodyY, width, graphY - bodyY - 3, COLORS.bg);
  for (x = 0; x < width; x += 24) {
    surface.drawLine(x, bodyY, x, graphY - 3, COLORS.grid);
  }
  for (y = bodyY; y < graphY - 2; y += 20) {
    surface.drawLine(0, y, width - 1, y, COLORS.grid);
  }
  for (i = 0; i < 12; i += 1) {
    x = ((frame * 5 + i * 27) % (width + 28)) - 28;
    h = 20 + ((frame * 4 + i * 19) % 92);
    y = graphY - 10 - h;
    if (y < bodyY + 4) {
      y = bodyY + 4;
      h = graphY - y - 10;
    }
    surface.fillRect(x, y, 24, h, BAR_COLORS[i % BAR_COLORS.length]);
    surface.drawRect(x, y, 24, h, COLORS.text);
  }

  x1 = 10 + pingPong(frame * 6, width - 48);
  y1 = bodyY + 16 + pingPong(frame * 4, graphY - bodyY - 62);
  x2 = 14 + pingPong(frame * 9 + 70, width - 66);
  y2 = bodyY + 24 + pingPong(frame * 5 + 30, graphY - bodyY - 70);
  surface.fillRect(0, bodyY + ((frame * 4) % (graphY - bodyY - 8)), width, 2, COLORS.dim);
  surface.fillRect(x1, y1, 42, 22, COLORS.yellow);
  surface.drawText(x1 + 10, y1 + 7, "JS", { color: COLORS.bg, spacing: 0 });
  surface.fillRect(x2, y2, 54, 18, COLORS.magenta);
  surface.drawText(x2 + 7, y2 + 5, "SPI", { color: COLORS.text, spacing: 0 });
  surface.drawRect(x2 - 2, y2 - 2, 58, 22, COLORS.orange);

  if (cjk12 && cjk16 && cjk24) {
    surface.drawText(10, bodyY + 8, "性能", { color: COLORS.yellow, font: cjk24 });
    surface.drawText(70, bodyY + 10, "中文显示", { color: COLORS.text, font: cjk16 });
    surface.drawText(70, bodyY + 30, "帧率 延迟 测试正常", { color: COLORS.green, font: cjk12 });
  }
}

function partialLayout() {
  var areaX = 12;
  var areaY = bodyY + 10;
  var areaW = width - 24;
  var areaH = graphY - areaY - 10;
  var box = 34;

  return {
    areaX: areaX,
    areaY: areaY,
    areaW: areaW,
    areaH: areaH,
    playX: areaX + 8,
    playY: areaY + 30,
    playW: Math.max(box, areaW - 16),
    playH: Math.max(box, areaH - 38),
    box: box
  };
}

function partialBox(frameValue) {
  var layout = partialLayout();

  return {
    x: layout.playX + pingPong(frameValue * 8, layout.playW - layout.box),
    y: layout.playY + pingPong(frameValue * 5, layout.playH - layout.box),
    w: layout.box,
    h: layout.box
  };
}

function drawPartialBackground(surface) {
  var layout = partialLayout();
  var i;
  var px;

  surface.fillRect(layout.areaX, layout.areaY, layout.areaW, layout.areaH, COLORS.bg);
  surface.drawRect(layout.areaX, layout.areaY, layout.areaW, layout.areaH, COLORS.dim);
  for (i = 0; i < 5; i += 1) {
    px = layout.areaX + 12 + i * ((layout.areaW - 24) / 4 | 0);
    surface.fillRect(px, layout.areaY + 4, 2, 12, BAR_COLORS[i]);
  }
  surface.drawText(layout.areaX + 8, layout.areaY + 8, "PARTIAL RECT TEST", { color: COLORS.text, spacing: 0 });
}

function drawPartialBox(surface, box) {
  surface.fillRect(box.x, box.y, box.w, box.h, COLORS.cyan);
  surface.drawRect(box.x + 4, box.y + 4, box.w - 8, box.h - 8, COLORS.text);
  surface.drawText(box.x + 8, box.y + 13, "P", { color: COLORS.bg, spacing: 0 });
}

function drawPartialScene(surface, dirty, fullFrame) {
  var box = partialBox(frame);
  var changed = unionRect(partialLastBox, box);

  if (fullFrame) {
    drawPartialBackground(surface);
  }
  if (changed) {
    surface.fillRect(changed.x, changed.y, changed.w, changed.h, COLORS.bg);
    drawPartialBox(surface, box);
    rect(dirty, changed.x, changed.y, changed.w, changed.h);
  }
  partialLastBox = box;
}

function drawTextScene(surface, dirty) {
  var y = bodyY + 8;
  var panelH = graphY - y - 8;
  var n = frame % 1000;

  surface.fillRect(8, y, width - 16, panelH, COLORS.bg);
  surface.drawRect(8, y, width - 16, panelH, COLORS.dim);
  surface.drawText(16, y + 8, "TEXT STRESS " + pad(n, 3), { color: COLORS.yellow, spacing: 0 });
  surface.drawText(16, y + 22, "ASCII 0123456789 ABCD", { color: COLORS.text, spacing: 0 });
  surface.drawText(16, y + 36, "FRAME " + frame + " HEAP " + (esp32.freeHeap() / 1024 | 0) + "K", { color: COLORS.cyan, spacing: 0 });
  if (cjk12 && cjk16 && cjk24) {
    surface.drawText(16, y + 54, "中文显示", { color: COLORS.green, font: cjk16 });
    surface.drawText(16, y + 76, "性能 测试 延迟", { color: COLORS.orange, font: cjk12 });
    surface.drawText(118, y + 64, "帧率", { color: COLORS.magenta, font: cjk24 });
  }
  rect(dirty, 8, y, width - 16, panelH);
}

function drawShapeScene(surface, dirty) {
  var areaX = 6;
  var areaY = bodyY + 4;
  var areaW = width - 12;
  var areaH = graphY - areaY - 6;
  var cx = areaX + areaW / 2 | 0;
  var cy = areaY + areaH / 2 | 0;
  var pulse = pingPong(frame * 2, 18);
  var spin = frame % 96;
  var star = [];
  var i;
  var angle;
  var radius;
  var px;
  var py;
  var stageUs;

  stageUs = esp32.micros();
  surface.fillRect(areaX, areaY, areaW, areaH, COLORS.bg);
  surface.drawRoundRect(areaX, areaY, areaW, areaH, 10, COLORS.dim);
  surface.fillRoundRect(areaX + 6, areaY + 6, 58, 22, 7, COLORS.panel2);
  surface.drawText(areaX + 13, areaY + 13, "SHAPES", { color: COLORS.yellow, spacing: 0 });
  addShapeTime("bg", stageUs);

  stageUs = esp32.micros();
  surface.fillCircle(areaX + 34 + pulse, areaY + 58, 18, COLORS.blue);
  surface.drawCircle(areaX + 34 + pulse, areaY + 58, 23, COLORS.cyan);
  surface.fillEllipse(width - 48, areaY + 52 + pingPong(frame, 18), 30, 14, COLORS.magenta);
  surface.drawEllipse(width - 48, areaY + 52 + pingPong(frame, 18), 38, 20, COLORS.text, { segments: 28 });
  addShapeTime("oval", stageUs);

  stageUs = esp32.micros();
  for (i = 0; i < 10; i += 1) {
    angle = (Math.PI * 2 * (i * 9 + spin)) / 96;
    radius = (i & 1) ? 18 : 36;
    px = cx + Math.round(Math.cos(angle) * radius);
    py = cy + Math.round(Math.sin(angle) * radius);
    star.push([px, py]);
  }
  surface.fillPolygon(star, COLORS.green);
  surface.drawPolygon(star, COLORS.text);
  addShapeTime("polygon", stageUs);

  stageUs = esp32.micros();
  surface.drawQuadraticBezier(
    areaX + 12,
    graphY - 34,
    cx,
    areaY + 18 + pingPong(frame * 3, areaH - 44),
    areaX + areaW - 12,
    graphY - 34,
    COLORS.orange,
    { segments: 28 }
  );
  surface.drawCubicBezier(
    areaX + 8,
    graphY - 12,
    areaX + 44,
    areaY + 26,
    areaX + areaW - 54,
    graphY - 66,
    areaX + areaW - 8,
    graphY - 12,
    COLORS.red,
    { segments: 34 }
  );
  addShapeTime("curve", stageUs);

  stageUs = esp32.micros();
  surface.fillTriangle(cx - 18, areaY + 18, cx + 20, areaY + 22, cx + 2, areaY + 52, COLORS.cyan);
  surface.drawTriangle(cx - 18, areaY + 18, cx + 20, areaY + 22, cx + 2, areaY + 52, COLORS.bg);
  addShapeTime("triangle", stageUs);
  rect(dirty, areaX, areaY, areaW, areaH);
}

function drawGraph(surface, dirty) {
  var i;
  var sample;
  var min = 0;
  var max = 0;
  var range;
  var ratio;
  var barH;
  var x;
  var color;
  var w = graphW / samples;

  surface.fillRect(graphX - 2, graphY - 2, graphW + 4, GRAPH_H + 1, COLORS.bg);
  surface.drawRect(graphX - 2, graphY - 2, graphW + 4, GRAPH_H + 1, COLORS.dim);
  for (i = 0; i < samples; i += 1) {
    sample = sampleAt(i);
    if (sample <= 0) {
      continue;
    }
    if (min === 0 || sample < min) {
      min = sample;
    }
    if (sample > max) {
      max = sample;
    }
  }
  range = max - min;
  if (range < 4000) {
    range = 4000;
  }
  surface.drawText(graphX, graphY - 13, "LAT " + ms(min) + "-" + ms(max) + "MS", { color: COLORS.text, spacing: 0 });
  for (i = 0; i < samples; i += 1) {
    sample = sampleAt(i);
    if (sample <= 0) {
      continue;
    }
    ratio = ((sample - min) * 1000 / range) | 0;
    if (ratio < 0) {
      ratio = 0;
    }
    if (ratio > 1000) {
      ratio = 1000;
    }
    barH = 3 + (((GRAPH_H - 8) * ratio) / 1000 | 0);
    color = ratio > 780 ? COLORS.red : (ratio > 520 ? COLORS.orange : COLORS.green);
    x = graphX + (i * w | 0);
    surface.drawLine(x, graphBottom, x, graphBottom - barH, color);
  }
  rect(dirty, 0, graphY - 15, width, height - graphY + 15);
}

function flushRects(dirty, fullFlush, options) {
  if (fullFlush) {
    screen.flush();
    return;
  }
  screen.present(dirty, options);
}

function publish(nowUs) {
  var wallUs = nowUs - reportLastUs;
  var frames = reportFrames || 1;
  var stats = screen.stats();
  var commandStats = lastCommandStats;

  latest.fps = wallUs > 0 ? (reportFrames * 1000000) / wallUs : 0;
  latest.frameUs = reportFrameUs / frames;
  latest.drawUs = reportDrawUs / frames;
  latest.flushUs = reportFlushUs / frames;
  latest.maxFrameUs = reportMaxFrameUs;
  latest.flushCalls = stats.presents;
  latest.flushTotalUs = stats.totalUs / frames;
  latest.windowUs = stats.panelUs / frames;
  latest.pixelUs = stats.prepareUs / frames;
  latest.dataUs = stats.transferUs / frames;
  latest.directFlushes = stats.directTransfers;
  latest.chunks = stats.chunks;
  latest.pixels = stats.pixels;
  latest.bytes = stats.bytes;
  latest.heap = esp32.freeHeap();
  latest.batch = lastBatch;
  latest.commandCount = lastBatch && commandStats ? commandStats.count : 0;
  latest.commandTextBytes = lastBatch && commandStats ? commandStats.textBytes : 0;
  latestShape.bgUs = reportShapeBgUs / frames;
  latestShape.ovalUs = reportShapeOvalUs / frames;
  latestShape.polygonUs = reportShapePolygonUs / frames;
  latestShape.curveUs = reportShapeCurveUs / frames;
  latestShape.triangleUs = reportShapeTriangleUs / frames;

  print("[display:perf]",
        "mode=" + activeMode().name,
        "fps=" + fmt1(latest.fps),
        "lat_ms=" + ms(latest.frameUs),
        "last_ms=" + ms(lastFrameUs),
        "draw_ms=" + ms(latest.drawUs),
        "flush_ms=" + ms(latest.flushUs),
        "flush_core_ms=" + ms(latest.flushTotalUs),
        "win_ms=" + ms(latest.windowUs),
        "prep_ms=" + ms(latest.pixelUs),
        "spi_ms=" + ms(latest.dataUs),
        "max_ms=" + ms(latest.maxFrameUs),
        "calls=" + latest.flushCalls,
        "direct=" + latest.directFlushes,
        "chunks=" + latest.chunks,
        "pixels=" + latest.pixels,
        "bytes=" + latest.bytes,
        "batch=" + (latest.batch ? "cmd" : "direct"),
        "cmds=" + latest.commandCount,
        "heap=" + latest.heap);

  if (activeMode().name === "SHAPE") {
    print("[display:shape]",
          "bg_ms=" + ms(latestShape.bgUs),
          "oval_ms=" + ms(latestShape.ovalUs),
          "poly_ms=" + ms(latestShape.polygonUs),
          "curve_ms=" + ms(latestShape.curveUs),
          "tri_ms=" + ms(latestShape.triangleUs));
  }

  reportFrames = 0;
  reportFrameUs = 0;
  reportDrawUs = 0;
  reportFlushUs = 0;
  reportMaxFrameUs = 0;
  reportShapeBgUs = 0;
  reportShapeOvalUs = 0;
  reportShapePolygonUs = 0;
  reportShapeCurveUs = 0;
  reportShapeTriangleUs = 0;
  reportLastUs = nowUs;
  screen.resetStats();
}

function drawFrame() {
  var frameStartUs = esp32.micros();
  var drawStartUs = frameStartUs;
  var flushStartUs;
  var nowUs;
  var dirty = [];
  var mode = activeMode();
  var partialMode;
  var drawPartialHudMetrics;
  var drawOverlay;
  var batch = null;
  var surface = screen;

  if (PERF.autoCycle !== false && frameStartUs - modeStartUs >= MODE_SECONDS * 1000000) {
    nextMode();
    mode = activeMode();
  }

  partialMode = mode.name === "PART";
  if (shouldUseBatch(mode) && screen.supports("batch")) {
    batch = screen.beginBatch();
    if (batch) {
      surface = batch;
    }
  }
  lastBatch = !!batch;
  drawOverlay = !partialMode ||
    forceFullFrame ||
    (PARTIAL_OVERLAY_MS > 0 && frameStartUs - partialOverlayUs >= PARTIAL_OVERLAY_MS * 1000);
  drawPartialHudMetrics = partialMode &&
    !drawOverlay &&
    PARTIAL_HUD_MS > 0 &&
    frameStartUs - partialHudUs >= PARTIAL_HUD_MS * 1000;

  if (forceFullFrame) {
    drawBase(surface);
  }
  if (drawOverlay) {
    drawHud(surface, dirty);
    partialHudUs = frameStartUs;
  } else if (drawPartialHudMetrics) {
    drawPartialHud(surface, dirty);
    partialHudUs = frameStartUs;
  }
  if (mode.name === "FULL") {
    drawFullScene(surface);
  } else if (mode.name === "PART") {
    drawPartialScene(surface, dirty, forceFullFrame);
  } else if (mode.name === "SHAPE") {
    drawShapeScene(surface, dirty);
  } else {
    drawTextScene(surface, dirty);
  }
  if (drawOverlay) {
    drawGraph(surface, dirty);
    partialOverlayUs = frameStartUs;
  }
  if (batch) {
    screen.endBatch(batch);
    lastCommandStats = batch.stats();
  } else {
    lastCommandStats = null;
  }
  lastDrawUs = esp32.micros() - drawStartUs;

  flushStartUs = esp32.micros();
  flushRects(dirty, forceFullFrame || mode.fullFlush, partialMode ? { merge: false } : undefined);
  lastFlushUs = esp32.micros() - flushStartUs;
  lastFrameUs = esp32.micros() - frameStartUs;
  forceFullFrame = false;

  pushSample(lastFrameUs);
  reportFrames += 1;
  reportFrameUs += lastFrameUs;
  reportDrawUs += lastDrawUs;
  reportFlushUs += lastFlushUs;
  if (lastFrameUs > reportMaxFrameUs) {
    reportMaxFrameUs = lastFrameUs;
  }

  frame += 1;
  nowUs = esp32.micros();
  if (nowUs - reportLastUs >= 1000000) {
    publish(nowUs);
  }
}

function stop() {
  if (timer !== null) {
    clearTimeout(timer);
    timer = null;
  }
  print("[display:perf] stopped");
}

function runFrame() {
  timer = null;
  drawFrame();
  if (timer === null) {
    timer = setTimeout(runFrame, FRAME_DELAY_MS);
  }
}

cjk12 = loadCjk(12);
cjk16 = loadCjk(16);
cjk24 = loadCjk(24);
initHistory();
screen.resetStats();
setMode(0);
timer = setTimeout(runFrame, 0);

globalThis.displayPerf = {
  screen: screen,
  stats: latest,
  stop: stop,
  next: nextMode,
  setMode: setMode,
  modes: MODES
};

print("[display:perf] running", width + "x" + height,
      "chunk=" + TRANSFER_BYTES,
      "batch=" + (USE_BATCH ? "on" : "off"),
      "storage=dma-auto",
      "modes=FULL,PART,SHAPE,TEXT",
      "next=displayPerf.next()",
      "stop=displayPerf.stop()");
