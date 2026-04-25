load("_sys/display.js");

var PERF = globalThis.displayPerfConfig || {};
var TRANSFER_BYTES = PERF.transferBytes || 16384;
var HUD_H = 58;
var GRAPH_H = 36;
var MODE_SECONDS = PERF.modeSeconds || 12;
var FRAME_DELAY_MS = PERF.frameDelayMs === undefined ? 1 : PERF.frameDelayMs;

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

var displayConfig = globalThis.displayConfig || PERF.display || {
  driver: "wlk1501spi8p",
  sclk: spi.DEFAULT_SCLK,
  mosi: spi.DEFAULT_MOSI,
  miso: -1,
  cs: spi.DEFAULT_CS >= 0 ? spi.DEFAULT_CS : 2,
  dc: 4,
  reset: 5,
  backlight: 6,
  freqHz: 40000000,
  maxTransferSize: TRANSFER_BYTES,
  chunkBytes: TRANSFER_BYTES,
  perf: true,
  foreground: COLORS.text,
  background: COLORS.bg
};

var screen = display.open(displayConfig);
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
var latest = {
  fps: 0,
  frameUs: 0,
  drawUs: 0,
  flushUs: 0,
  maxFrameUs: 0,
  chunks: 0,
  bytes: 0,
  heap: 0
};

var MODES = [
  { name: "FULL", label: "FULL SCREEN", fullFlush: true },
  { name: "PART", label: "PARTIAL RECTS", fullFlush: false },
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

function setMode(index) {
  modeIndex = ((index % MODES.length) + MODES.length) % MODES.length;
  modeStartUs = esp32.micros();
  forceFullFrame = true;
  if (screen.resetPerf) {
    screen.resetPerf();
  }
  print("[display:perf]", "mode=" + activeMode().name);
}

function nextMode() {
  setMode(modeIndex + 1);
}

function drawBase() {
  var x;
  var y;

  screen.clear(COLORS.bg);
  screen.fillRect(0, 0, width, HUD_H, COLORS.panel);
  for (x = 0; x < width; x += 24) {
    screen.drawLine(x, bodyY, x, graphY - 3, COLORS.grid);
  }
  for (y = bodyY; y < graphY - 2; y += 20) {
    screen.drawLine(0, y, width - 1, y, COLORS.grid);
  }
}

function drawHud(dirty) {
  var mode = activeMode();
  var seconds = ((esp32.micros() - modeStartUs) / 1000000) | 0;

  screen.fillRect(0, 0, width, HUD_H, COLORS.panel);
  screen.drawText(6, 4, "DISPLAY PERF " + mode.name, { color: COLORS.yellow, spacing: 0 });
  screen.drawText(width - 70, 4, "F" + pad(frame, 5), { color: COLORS.text, spacing: 0 });
  screen.drawText(6, 18,
                  "FPS " + fmt1(latest.fps) +
                  "  LAT " + ms(lastFrameUs) + "MS" +
                  "  T" + pad(seconds, 2),
                  { color: COLORS.text, spacing: 0 });
  screen.drawText(6, 30,
                  "DRAW " + ms(lastDrawUs) +
                  "  FLUSH " + ms(lastFlushUs),
                  { color: COLORS.cyan, spacing: 0 });
  screen.drawText(6, 42,
                  "AVG " + ms(latest.frameUs) +
                  "  MAX " + ms(latest.maxFrameUs) +
                  "  KB " + kb(latest.bytes),
                  { color: COLORS.green, spacing: 0 });
  rect(dirty, 0, 0, width, HUD_H);
}

function drawFullScene() {
  var i;
  var x;
  var y;
  var h;
  var x1;
  var y1;
  var x2;
  var y2;

  screen.fillRect(0, bodyY, width, graphY - bodyY - 3, COLORS.bg);
  for (x = 0; x < width; x += 24) {
    screen.drawLine(x, bodyY, x, graphY - 3, COLORS.grid);
  }
  for (y = bodyY; y < graphY - 2; y += 20) {
    screen.drawLine(0, y, width - 1, y, COLORS.grid);
  }
  for (i = 0; i < 12; i += 1) {
    x = ((frame * 5 + i * 27) % (width + 28)) - 28;
    h = 20 + ((frame * 4 + i * 19) % 92);
    y = graphY - 10 - h;
    if (y < bodyY + 4) {
      y = bodyY + 4;
      h = graphY - y - 10;
    }
    screen.fillRect(x, y, 24, h, BAR_COLORS[i % BAR_COLORS.length]);
    screen.drawRect(x, y, 24, h, COLORS.text);
  }

  x1 = 10 + pingPong(frame * 6, width - 48);
  y1 = bodyY + 16 + pingPong(frame * 4, graphY - bodyY - 62);
  x2 = 14 + pingPong(frame * 9 + 70, width - 66);
  y2 = bodyY + 24 + pingPong(frame * 5 + 30, graphY - bodyY - 70);
  screen.fillRect(0, bodyY + ((frame * 4) % (graphY - bodyY - 8)), width, 2, COLORS.dim);
  screen.fillRect(x1, y1, 42, 22, COLORS.yellow);
  screen.drawText(x1 + 10, y1 + 7, "JS", { color: COLORS.bg, spacing: 0 });
  screen.fillRect(x2, y2, 54, 18, COLORS.magenta);
  screen.drawText(x2 + 7, y2 + 5, "SPI", { color: COLORS.text, spacing: 0 });
  screen.drawRect(x2 - 2, y2 - 2, 58, 22, COLORS.orange);

  if (cjk12 && cjk16 && cjk24) {
    screen.drawText(10, bodyY + 8, "性能", { color: COLORS.yellow, font: cjk24 });
    screen.drawText(70, bodyY + 10, "中文显示", { color: COLORS.text, font: cjk16 });
    screen.drawText(70, bodyY + 30, "帧率 延迟 测试正常", { color: COLORS.green, font: cjk12 });
  }
}

function drawPartialScene(dirty) {
  var areaX = 12;
  var areaY = bodyY + 10;
  var areaW = width - 24;
  var areaH = graphY - areaY - 10;
  var box = 34;
  var x = areaX + pingPong(frame * 8, areaW - box);
  var y = areaY + pingPong(frame * 5, areaH - box);
  var i;
  var px;

  screen.fillRect(areaX, areaY, areaW, areaH, COLORS.bg);
  screen.drawRect(areaX, areaY, areaW, areaH, COLORS.dim);
  for (i = 0; i < 5; i += 1) {
    px = areaX + ((frame * 11 + i * 41) % areaW);
    screen.fillRect(px, areaY + 2, 3, areaH - 4, BAR_COLORS[i]);
  }
  screen.fillRect(x, y, box, box, COLORS.cyan);
  screen.drawRect(x + 4, y + 4, box - 8, box - 8, COLORS.text);
  screen.drawText(x + 8, y + 13, "P", { color: COLORS.bg, spacing: 0 });
  screen.drawText(areaX + 8, areaY + 8, "flushRect " + areaW + "x" + areaH, { color: COLORS.text, spacing: 0 });
  rect(dirty, areaX, areaY, areaW, areaH);
}

function drawTextScene(dirty) {
  var y = bodyY + 8;
  var panelH = graphY - y - 8;
  var n = frame % 1000;

  screen.fillRect(8, y, width - 16, panelH, COLORS.bg);
  screen.drawRect(8, y, width - 16, panelH, COLORS.dim);
  screen.drawText(16, y + 8, "TEXT STRESS " + pad(n, 3), { color: COLORS.yellow, spacing: 0 });
  screen.drawText(16, y + 22, "ASCII 0123456789 ABCD", { color: COLORS.text, spacing: 0 });
  screen.drawText(16, y + 36, "FRAME " + frame + " HEAP " + (esp32.freeHeap() / 1024 | 0) + "K", { color: COLORS.cyan, spacing: 0 });
  if (cjk12 && cjk16 && cjk24) {
    screen.drawText(16, y + 54, "中文显示", { color: COLORS.green, font: cjk16 });
    screen.drawText(16, y + 76, "性能 测试 延迟", { color: COLORS.orange, font: cjk12 });
    screen.drawText(118, y + 64, "帧率", { color: COLORS.magenta, font: cjk24 });
  }
  rect(dirty, 8, y, width - 16, panelH);
}

function drawGraph(dirty) {
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

  screen.fillRect(graphX - 2, graphY - 2, graphW + 4, GRAPH_H + 1, COLORS.bg);
  screen.drawRect(graphX - 2, graphY - 2, graphW + 4, GRAPH_H + 1, COLORS.dim);
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
  screen.drawText(graphX, graphY - 13, "LAT " + ms(min) + "-" + ms(max) + "MS", { color: COLORS.text, spacing: 0 });
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
    screen.drawLine(x, graphBottom, x, graphBottom - barH, color);
  }
  rect(dirty, 0, graphY - 15, width, height - graphY + 15);
}

function flushRects(dirty, fullFlush) {
  var i;

  if (fullFlush || typeof screen.flushRect !== "function") {
    screen.flush();
    return;
  }
  for (i = 0; i < dirty.length; i += 1) {
    screen.flushRect(dirty[i].x, dirty[i].y, dirty[i].w, dirty[i].h);
  }
}

function publish(nowUs) {
  var wallUs = nowUs - reportLastUs;
  var frames = reportFrames || 1;
  var stats = screen.getPerf ? screen.getPerf() : null;

  latest.fps = wallUs > 0 ? (reportFrames * 1000000) / wallUs : 0;
  latest.frameUs = reportFrameUs / frames;
  latest.drawUs = reportDrawUs / frames;
  latest.flushUs = reportFlushUs / frames;
  latest.maxFrameUs = reportMaxFrameUs;
  latest.chunks = stats ? stats.chunks : 0;
  latest.bytes = stats ? stats.bytes : 0;
  latest.heap = esp32.freeHeap();

  print("[display:perf]",
        "mode=" + activeMode().name,
        "fps=" + fmt1(latest.fps),
        "lat_ms=" + ms(latest.frameUs),
        "last_ms=" + ms(lastFrameUs),
        "draw_ms=" + ms(latest.drawUs),
        "flush_ms=" + ms(latest.flushUs),
        "max_ms=" + ms(latest.maxFrameUs),
        "chunks=" + latest.chunks,
        "bytes=" + latest.bytes,
        "heap=" + latest.heap);

  reportFrames = 0;
  reportFrameUs = 0;
  reportDrawUs = 0;
  reportFlushUs = 0;
  reportMaxFrameUs = 0;
  reportLastUs = nowUs;
  if (screen.resetPerf) {
    screen.resetPerf();
  }
}

function drawFrame() {
  var frameStartUs = esp32.micros();
  var drawStartUs = frameStartUs;
  var flushStartUs;
  var nowUs;
  var dirty = [];
  var mode = activeMode();

  if (PERF.autoCycle !== false && frameStartUs - modeStartUs >= MODE_SECONDS * 1000000) {
    nextMode();
    mode = activeMode();
  }

  if (forceFullFrame) {
    drawBase();
  }
  drawHud(dirty);
  if (mode.name === "FULL") {
    drawFullScene();
  } else if (mode.name === "PART") {
    drawPartialScene(dirty);
  } else {
    drawTextScene(dirty);
  }
  drawGraph(dirty);
  lastDrawUs = esp32.micros() - drawStartUs;

  flushStartUs = esp32.micros();
  flushRects(dirty, forceFullFrame || mode.fullFlush);
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
if (screen.resetPerf) {
  screen.resetPerf();
}
screen.perfEnabled = true;
setMode(0);
drawFrame();
timer = setTimeout(runFrame, FRAME_DELAY_MS);

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
      "modes=FULL,PART,TEXT",
      "next=displayPerf.next()",
      "stop=displayPerf.stop()");
