(function (global) {
  var owns = Object.prototype.hasOwnProperty;
  var display = global.display;
  var ui = /** @type {ESP32QJS.UIModule} */ (global.ui || {});
  var contexts = [];
  var activeContext = null;
  var pendingInput = {};
  var emptyOptions = {};

  if (ui.__loaded) {
    return;
  }
  if (!display || !display.__loaded) {
    throw new Error("load(\"_sys/display.js\") before load(\"_sys/ui.js\")");
  }

  function own(obj, key) {
    return obj !== null && obj !== undefined && owns.call(obj, key);
  }

  function isArray(value) {
    return Object.prototype.toString.call(value) === "[object Array]";
  }

  function clampInt(value, minValue, maxValue) {
    var number = value | 0;

    if (number < minValue) {
      return minValue;
    }
    if (number > maxValue) {
      return maxValue;
    }
    return number;
  }

  function copyObject(source) {
    var target = {};
    var key;

    source = source || {};
    for (key in source) {
      if (own(source, key)) {
        target[key] = source[key];
      }
    }
    return target;
  }

  function optionStyle(options) {
    var style = options && options.style;

    return style && typeof style === "object" ? style : null;
  }

  function hasOption(options, key) {
    var style;

    if (options && own(options, key)) {
      return true;
    }
    style = optionStyle(options);
    return !!(style && own(style, key));
  }

  function optionValue(options, key, fallback) {
    var style;

    if (options && own(options, key)) {
      return options[key];
    }
    style = optionStyle(options);
    if (style && own(style, key)) {
      return style[key];
    }
    return fallback;
  }

  function normalizeInsets(value) {
    if (typeof value === "number") {
      return {
        top: value | 0,
        right: value | 0,
        bottom: value | 0,
        left: value | 0
      };
    }
    value = value || {};
    return {
      top: own(value, "top") ? value.top | 0 : (own(value, "y") ? value.y | 0 : 0),
      right: own(value, "right") ? value.right | 0 : (own(value, "x") ? value.x | 0 : 0),
      bottom: own(value, "bottom") ? value.bottom | 0 : (own(value, "y") ? value.y | 0 : 0),
      left: own(value, "left") ? value.left | 0 : (own(value, "x") ? value.x | 0 : 0)
    };
  }

  function surfaceForeground(surface) {
    if (own(surface, "foreground")) {
      return surface.foreground;
    }
    return surface.pixelFormat === "rgb565" ? display.rgb565(255, 255, 255) : display.mono1(1);
  }

  function surfaceBackground(surface) {
    if (own(surface, "background")) {
      return surface.background;
    }
    return surface.pixelFormat === "rgb565" ? display.rgb565(0, 0, 0) : display.mono1(0);
  }

  function packedColor(surface, value, fallback, apiName) {
    var color;

    if (value === undefined || value === null) {
      return fallback;
    }
    if (typeof value !== "number" || value !== value) {
      throw new Error(apiName + " expects a packed display color");
    }
    color = value | 0;
    if (surface.pixelFormat === "mono1" && (color < 0 || color > 1)) {
      throw new Error(apiName + " expects a display.mono1(...) color");
    }
    if (surface.pixelFormat === "rgb565" && (color < 0 || color > 0xffff)) {
      throw new Error(apiName + " expects a display.rgb565(...) color");
    }
    return color;
  }

  function rectFrom(surface, value, fallback) {
    var rect = fallback || { x: 0, y: 0, width: surface.width, height: surface.height };

    value = value || {};
    return {
      x: own(value, "x") ? value.x | 0 : rect.x,
      y: own(value, "y") ? value.y | 0 : rect.y,
      width: own(value, "width") ? value.width | 0 : rect.width,
      height: own(value, "height") ? value.height | 0 : rect.height
    };
  }

  function clampRect(surface, rect) {
    var x0 = clampInt(rect.x, 0, surface.width);
    var y0 = clampInt(rect.y, 0, surface.height);
    var x1 = clampInt(rect.x + rect.width, 0, surface.width);
    var y1 = clampInt(rect.y + rect.height, 0, surface.height);

    if (x1 <= x0 || y1 <= y0) {
      return null;
    }
    return {
      x: x0,
      y: y0,
      width: x1 - x0,
      height: y1 - y0
    };
  }

  function rectEquals(a, b) {
    return !!a && !!b &&
      a.x === b.x && a.y === b.y && a.width === b.width && a.height === b.height;
  }

  function rectContains(rect, x, y) {
    return !!rect &&
      x >= rect.x && y >= rect.y &&
      x < rect.x + rect.width && y < rect.y + rect.height;
  }

  function unionRect(a, b) {
    var x0;
    var y0;
    var x1;
    var y1;

    if (!a) {
      return b ? { x: b.x, y: b.y, width: b.width, height: b.height } : null;
    }
    if (!b) {
      return { x: a.x, y: a.y, width: a.width, height: a.height };
    }
    x0 = a.x < b.x ? a.x : b.x;
    y0 = a.y < b.y ? a.y : b.y;
    x1 = a.x + a.width > b.x + b.width ? a.x + a.width : b.x + b.width;
    y1 = a.y + a.height > b.y + b.height ? a.y + a.height : b.y + b.height;
    return {
      x: x0,
      y: y0,
      width: x1 - x0,
      height: y1 - y0
    };
  }

  function rectArea(rect) {
    return rect ? rect.width * rect.height : 0;
  }

  function rectSignature(rect) {
    if (!rect) {
      return "";
    }
    return rect.x + "," + rect.y + "," + rect.width + "," + rect.height;
  }

  function valueSignature(value) {
    var out = "";
    var i;
    var key;

    if (value === null || value === undefined) {
      return "";
    }
    if (typeof value === "string" || typeof value === "number" || typeof value === "boolean") {
      return String(value);
    }
    if (isArray(value)) {
      for (i = 0; i < value.length; i += 1) {
        if (i > 0) {
          out += ",";
        }
        out += valueSignature(value[i]);
      }
      return "[" + out + "]";
    }
    if (typeof value === "object") {
      for (key in value) {
        if (own(value, key)) {
          out += key + ":" + valueSignature(value[key]) + ";";
        }
      }
      return out;
    }
    return String(value);
  }

  var visualOptionKeys = [
    "x",
    "y",
    "width",
    "height",
    "padding",
    "gap",
    "align",
    "background",
    "border",
    "borderColor",
    "radius",
    "borderRadius",
    "color",
    "font",
    "spacing",
    "textAlign",
    "valign",
    "outline",
    "outlineColor",
    "outlineWidth"
  ];
  var styleOptionKeys = [
    "padding",
    "align",
    "background",
    "border",
    "borderColor",
    "radius",
    "borderRadius",
    "color",
    "font",
    "spacing",
    "textAlign",
    "valign",
    "outline",
    "outlineColor",
    "outlineWidth"
  ];
  var visualOptionSet = keySet(visualOptionKeys);
  var styleOptionSet = keySet(styleOptionKeys);

  function keySet(keys) {
    var set = {};
    var i;

    for (i = 0; i < keys.length; i += 1) {
      set[keys[i]] = true;
    }
    return set;
  }

  function hasKey(keys, key) {
    var i;

    for (i = 0; keys && i < keys.length; i += 1) {
      if (keys[i] === key) {
        return true;
      }
    }
    return false;
  }

  function optionSignature(options, extraKeys) {
    var out = "";
    var style;
    var key;

    if (!options || options === emptyOptions) {
      return "";
    }
    for (key in options) {
      if (own(options, key) && (visualOptionSet[key] || hasKey(extraKeys, key))) {
        out += key + ":" + valueSignature(options[key]) + ";";
      }
    }
    style = optionStyle(options);
    if (style) {
      if (own(style, "__sig")) {
        out += "style:" + String(style.__sig) + ";";
      } else {
        for (key in style) {
          if (own(style, key) && styleOptionSet[key] && !own(options, key)) {
            out += key + ":" + valueSignature(style[key]) + ";";
          }
        }
      }
    }
    return out;
  }

  function listSignature(list) {
    var out = "";
    var i;

    list = list || [];
    for (i = 0; i < list.length; i += 1) {
      if (i > 0) {
        out += ",";
      }
      out += String(list[i]);
    }
    return out;
  }

  function normalizeInput(input) {
    var touch;

    input = input || {};
    touch = input.touch || null;
    if (touch) {
      touch = {
        x: own(touch, "x") ? touch.x | 0 : 0,
        y: own(touch, "y") ? touch.y | 0 : 0,
        pressed: !!touch.pressed
      };
    }
    return {
      up: !!input.up,
      down: !!input.down,
      left: !!input.left,
      right: !!input.right,
      ok: !!input.ok,
      back: !!input.back,
      encoderDelta: own(input, "encoderDelta") ? input.encoderDelta | 0 : 0,
      touch: touch
    };
  }

  function inputEdges(input, previous) {
    previous = previous || normalizeInput();
    return {
      up: input.up && !previous.up,
      down: input.down && !previous.down,
      left: input.left && !previous.left,
      right: input.right && !previous.right,
      ok: input.ok && !previous.ok,
      back: input.back && !previous.back,
      encoderDelta: input.encoderDelta,
      touchPressed: !!(input.touch && input.touch.pressed &&
        (!previous.touch || !previous.touch.pressed))
    };
  }

  function inputRevealsFocus(edges) {
    return !!(edges && (edges.up || edges.down || edges.left || edges.right ||
      edges.ok || edges.back || edges.encoderDelta || edges.touchPressed));
  }

  function rgbTheme() {
    return {
      background: display.rgb565(0, 0, 0),
      foreground: display.rgb565(255, 255, 255),
      muted: display.rgb565(120, 120, 120),
      panel: display.rgb565(20, 24, 28),
      control: display.rgb565(30, 36, 42),
      border: display.rgb565(82, 90, 96),
      accent: display.rgb565(0, 176, 255),
      accentText: display.rgb565(0, 0, 0),
      danger: display.rgb565(255, 64, 64),
      focus: display.rgb565(255, 212, 64),
      pressed: display.rgb565(0, 120, 190)
    };
  }

  function monoTheme() {
    return {
      background: display.mono1(0),
      foreground: display.mono1(1),
      muted: display.mono1(1),
      panel: display.mono1(0),
      control: display.mono1(0),
      border: display.mono1(1),
      accent: display.mono1(1),
      accentText: display.mono1(0),
      danger: display.mono1(1),
      focus: display.mono1(1),
      pressed: display.mono1(1)
    };
  }

  function resolveTheme(surface, theme) {
    var defaults = surface.pixelFormat === "rgb565" ? ui.theme.dark : ui.theme.mono;
    var base;
    var key;

    if (!theme || theme === emptyOptions) {
      return defaults;
    }
    base = copyObject(defaults);
    for (key in theme) {
      if (own(theme, key)) {
        base[key] = theme[key];
      }
    }
    return base;
  }

  function resolveMetrics(context, surface, options) {
    var font = optionValue(options, "font", null);
    var key = fontMeasureKey(font);
    var metrics;

    if (context.metricsKey === key && context.metrics) {
      return context.metrics;
    }
    metrics = font ? surface.measureText("M", { font: font }) : surface.measureText("M");
    context.metricsKey = key;
    context.metrics = {
      lineHeight: metrics.height || 8,
      textHeight: metrics.height || 8,
      textWidth: metrics.width || 6
    };
    return context.metrics;
  }

  function findContext(surface) {
    var i;

    for (i = 0; i < contexts.length; i += 1) {
      if (contexts[i].surface === surface) {
        return contexts[i];
      }
    }
    return null;
  }

  function createContext(surface) {
    var context = {
      surface: surface,
      draw: null,
      started: false,
      state: {},
      focusedId: null,
      focusVisible: false,
      focusOrder: [],
      previousFocusOrder: [],
      editingId: null,
      prevInput: normalizeInput(),
      input: normalizeInput(),
      edges: inputEdges(normalizeInput(), normalizeInput()),
      stack: [],
      rootFrame: null,
      dirty: null,
      dirtyRects: [],
      theme: null,
      nextAutoId: 0,
      partial: true,
      flush: true,
      dirtyFlushOptions: null,
      gcBeforeFlush: true,
      forceFull: false,
      measureCache: {},
      measureCacheCount: 0,
      metricsKey: "",
      metrics: {
        lineHeight: 8,
        textHeight: 8,
        textWidth: 6
      }
    };

    contexts.push(context);
    return context;
  }

  function currentContext() {
    if (!activeContext) {
      throw new Error("ui.begin(surface) must be called before drawing controls");
    }
    return activeContext;
  }

  function drawTarget(context) {
    return context.draw || context.surface;
  }

  function topLayout(context) {
    return context.stack[context.stack.length - 1];
  }

  function currentBackground(context) {
    var layout = topLayout(context);

    if (layout && own(layout, "background")) {
      return layout.background;
    }
    return context.theme.background;
  }

  function fontMeasureKey(font) {
    if (!font) {
      return "default";
    }
    return [
      font.name || "font",
      font.width || 0,
      font.height || 0,
      font.advance || 0,
      font.lineHeight || 0
    ].join(":");
  }

  function measureText(context, text, options) {
    var value = String(text);
    var style = {
      spacing: optionValue(options, "spacing", 0)
    };
    var metrics;
    var cacheKey;

    if (optionValue(options, "font", null)) {
      style.font = optionValue(options, "font", null);
    }
    if (value.length <= 64) {
      cacheKey = fontMeasureKey(style.font) + "|" + style.spacing + "|" + value;
      metrics = context.measureCache[cacheKey];
      if (metrics) {
        return metrics;
      }
    }
    metrics = context.surface.measureText(value, style);

    if (metrics && metrics.height > 0) {
      if (cacheKey) {
        if (context.measureCacheCount >= 128) {
          context.measureCache = {};
          context.measureCacheCount = 0;
        }
        context.measureCache[cacheKey] = metrics;
        context.measureCacheCount += 1;
      }
      return metrics;
    }
    metrics = {
      width: String(text).length * context.metrics.textWidth,
      height: context.metrics.textHeight,
      lines: 1
    };
    if (cacheKey) {
      if (context.measureCacheCount >= 128) {
        context.measureCache = {};
        context.measureCacheCount = 0;
      }
      context.measureCache[cacheKey] = metrics;
      context.measureCacheCount += 1;
    }
    return metrics;
  }

  function explicitSize(value) {
    if (typeof value === "number") {
      return value | 0;
    }
    return null;
  }

  function remainingWidth(layout) {
    var end;

    if (layout.type === "row") {
      end = layout.content.x + layout.content.width;
      return end - layout.cursorX;
    }
    return layout.content.width;
  }

  function remainingHeight(layout) {
    var end;

    if (layout.type === "column" || layout.type === "root") {
      end = layout.content.y + layout.content.height;
      return end - layout.cursorY;
    }
    return layout.content.height;
  }

  function alignOffset(align, freeSpace) {
    if (freeSpace <= 0) {
      return 0;
    }
    if (align === "center") {
      return freeSpace >> 1;
    }
    if (align === "end") {
      return freeSpace;
    }
    return 0;
  }

  function allocateRect(context, options, defaultSize) {
    var layout = topLayout(context);
    var width;
    var height;
    var rect;
    var content;
    var crossAlign;
    var x;
    var y;

    options = options || emptyOptions;
    defaultSize = defaultSize || {};
    content = layout.content;
    if (layout.type === "root") {
      width = explicitSize(options.width);
      height = explicitSize(options.height);
      rect = {
        x: own(options, "x") ? options.x | 0 : content.x,
        y: own(options, "y") ? options.y | 0 : content.y,
        width: width !== null ? width : content.width,
        height: height !== null ? height : content.height
      };
      return rect;
    }

    if (layout.type === "row") {
      width = explicitSize(options.width);
      height = explicitSize(options.height);
      if (width === null) {
        width = own(defaultSize, "width") ? defaultSize.width | 0 : remainingWidth(layout);
      }
      if (height === null) {
        height = own(defaultSize, "height") ? defaultSize.height | 0 : content.height;
      }
      crossAlign = own(options, "align") ? options.align : layout.align;
      x = layout.cursorX;
      y = content.y + alignOffset(crossAlign, content.height - height);
      rect = {
        x: x,
        y: y,
        width: width,
        height: height
      };
      layout.cursorX += width + layout.gap;
      return rect;
    }

    width = explicitSize(options.width);
    height = explicitSize(options.height);
    if (width === null) {
      width = own(defaultSize, "width") ? defaultSize.width | 0 : content.width;
    }
    if (height === null) {
      height = own(defaultSize, "height") ? defaultSize.height | 0 : remainingHeight(layout);
    }
    crossAlign = own(options, "align") ? options.align : layout.align;
    x = content.x + alignOffset(crossAlign, content.width - width);
    y = layout.cursorY;
    rect = {
      x: x,
      y: y,
      width: width,
      height: height
    };
    layout.cursorY += height + layout.gap;
    return rect;
  }

  function markDirty(context, rect) {
    var list;
    var i;
    var existing;
    var merged;

    rect = clampRect(context.surface, rect);
    if (!rect) {
      return;
    }
    context.dirty = unionRect(context.dirty, rect);
    list = context.dirtyRects;
    if (!list) {
      list = [];
      context.dirtyRects = list;
    }
    for (i = 0; i < list.length; i += 1) {
      existing = list[i];
      merged = unionRect(existing, rect);
      if (merged.width * merged.height <=
          (existing.width * existing.height + rect.width * rect.height + 64)) {
        list[i] = merged;
        return;
      }
    }
    list.push(rect);
  }

  function dirtyFlushOptions(surface, options) {
    var nested = options && own(options, "dirty") ? options.dirty : emptyOptions;
    var pixelBudget = own(options, "dirtyMergePixelBudget")
      ? options.dirtyMergePixelBudget
      : (own(nested, "mergePixelBudget") ? nested.mergePixelBudget : surface.width * 24);
    var maxRects = own(options, "dirtyMaxRects")
      ? options.dirtyMaxRects
      : (own(nested, "maxRects") ? nested.maxRects : 0);

    return {
      merge: own(options, "dirtyMerge")
        ? options.dirtyMerge !== false
        : (own(nested, "merge") ? nested.merge !== false : true),
      mergeCoverage: own(options, "dirtyMergeCoverage")
        ? +options.dirtyMergeCoverage
        : (own(nested, "mergeCoverage") ? +nested.mergeCoverage : 0.72),
      mergeAreaRatio: own(options, "dirtyMergeAreaRatio")
        ? +options.dirtyMergeAreaRatio
        : (own(nested, "mergeAreaRatio") ? +nested.mergeAreaRatio : 1.22),
      mergePixelBudget: pixelBudget | 0,
      maxRects: maxRects | 0
    };
  }

  function copyDirtyRects(rects) {
    var list = [];
    var rect;
    var i;

    for (i = 0; rects && i < rects.length; i += 1) {
      rect = rects[i];
      if (rect && rect.width > 0 && rect.height > 0) {
        list.push({
          x: rect.x,
          y: rect.y,
          width: rect.width,
          height: rect.height
        });
      }
    }
    return list;
  }

  function totalRectArea(rects) {
    var area = 0;
    var i;

    for (i = 0; rects && i < rects.length; i += 1) {
      area += rectArea(rects[i]);
    }
    return area;
  }

  function compactDirtyRects(rects, pixelBudget) {
    var bestA;
    var bestB;
    var bestRect;
    var bestExtra;
    var i;
    var j;
    var merged;
    var extra;

    if (!rects || rects.length <= 1) {
      return rects || [];
    }
    if (pixelBudget < 0) {
      pixelBudget = 0;
    }
    while (rects.length > 1) {
      bestA = -1;
      bestB = -1;
      bestRect = null;
      bestExtra = 2147483647;
      for (i = 0; i < rects.length - 1; i += 1) {
        for (j = i + 1; j < rects.length; j += 1) {
          merged = unionRect(rects[i], rects[j]);
          extra = rectArea(merged) - rectArea(rects[i]) - rectArea(rects[j]);
          if (extra < bestExtra) {
            bestA = i;
            bestB = j;
            bestRect = merged;
            bestExtra = extra;
          }
        }
      }
      if (bestA < 0 || bestExtra > pixelBudget) {
        break;
      }
      rects[bestA] = bestRect;
      rects.splice(bestB, 1);
    }
    return rects;
  }

  function resolveDirtyFlushRects(context, dirty, dirtyRects) {
    var options = context.dirtyFlushOptions || dirtyFlushOptions(context.surface, emptyOptions);
    var rects;
    var totalArea;
    var dirtyArea;
    var screenArea;

    if (!dirty) {
      return [];
    }
    if (context.forceFull || !dirtyRects || dirtyRects.length === 0) {
      return [dirty];
    }
    rects = copyDirtyRects(dirtyRects);
    if (rects.length <= 1 || !options.merge) {
      return rects.length > 0 ? rects : [dirty];
    }
    totalArea = totalRectArea(rects);
    dirtyArea = rectArea(dirty);
    screenArea = context.surface.width * context.surface.height;
    if (totalArea >= screenArea * options.mergeCoverage ||
        dirtyArea <= totalArea * options.mergeAreaRatio ||
        dirtyArea - totalArea <= options.mergePixelBudget * (rects.length - 1)) {
      return [dirty];
    }
    rects = compactDirtyRects(rects, options.mergePixelBudget);
    if (options.maxRects > 0 && rects.length > options.maxRects) {
      return [dirty];
    }
    totalArea = totalRectArea(rects);
    if (dirtyArea <= totalArea * options.mergeAreaRatio ||
        dirtyArea - totalArea <= options.mergePixelBudget * (rects.length - 1)) {
      return [dirty];
    }
    return rects;
  }

  function autoId(context, type) {
    var id = type + "#" + context.nextAutoId;

    context.nextAutoId += 1;
    return id;
  }

  function stateFor(context, key) {
    if (!context.state[key]) {
      context.state[key] = {};
    }
    return context.state[key];
  }

  function setFocusVisible(context, visible) {
    if (context.focusVisible !== visible) {
      context.focusVisible = visible;
      if (context.focusedId) {
        stateFor(context, context.focusedId).focusDirty = true;
      }
    }
  }

  function registerFocus(context, id) {
    var state;

    context.focusOrder.push(id);
    if (!context.focusedId) {
      context.focusedId = id;
      state = stateFor(context, id);
      state.focusDirty = true;
    }
    return context.focusedId === id;
  }

  function focusIndex(order, id) {
    var i;

    for (i = 0; i < order.length; i += 1) {
      if (order[i] === id) {
        return i;
      }
    }
    return -1;
  }

  function updateFocusFromInput(context) {
    var order = context.previousFocusOrder;
    var index;
    var move = 0;
    var previous;
    var next;

    if (!order || order.length === 0 || context.editingId) {
      return;
    }
    if (context.edges.down) {
      move = 1;
    } else if (context.edges.up) {
      move = -1;
    }
    if (move === 0) {
      return;
    }
    previous = context.focusedId;
    index = focusIndex(order, previous);
    if (index < 0) {
      index = 0;
    } else {
      index = (index + move + order.length) % order.length;
    }
    next = order[index];
    if (next !== previous) {
      context.focusedId = next;
      if (previous) {
        stateFor(context, previous).focusDirty = true;
      }
      stateFor(context, next).focusDirty = true;
    }
  }

  function touchPressedIn(context, rect) {
    return !!(context.edges.touchPressed && context.input.touch &&
      rectContains(rect, context.input.touch.x, context.input.touch.y));
  }

  function beginControl(context, type, id, rect, signature, focusable) {
    var key = id || autoId(context, type);
    var state = stateFor(context, key);
    var focused = false;
    var dirty = context.forceFull || state.signature !== signature ||
      !rectEquals(state.rect, rect) || !!state.focusDirty;

    if (state.rect && !rectEquals(state.rect, rect)) {
      markDirty(context, state.rect);
    }
    if (focusable) {
      focused = registerFocus(context, key);
      if (state.focused !== focused) {
        dirty = true;
      }
    }
    if (touchPressedIn(context, rect)) {
      if (focusable && context.focusedId !== key) {
        if (context.focusedId) {
          stateFor(context, context.focusedId).focusDirty = true;
        }
        context.focusedId = key;
        focused = true;
        dirty = true;
      }
    }
    if (dirty) {
      markDirty(context, rect);
    }
    state.signature = signature;
    state.rect = {
      x: rect.x,
      y: rect.y,
      width: rect.width,
      height: rect.height
    };
    state.focused = focused;
    state.focusDirty = false;
    return {
      key: key,
      state: state,
      focused: focused,
      dirty: dirty || context.forceFull,
      touched: touchPressedIn(context, rect)
    };
  }

  function fillRect(surface, rect, color) {
    if (rect.width > 0 && rect.height > 0) {
      surface.fillRect(rect.x, rect.y, rect.width, rect.height, color);
    }
  }

  function drawBorder(surface, rect, color) {
    if (rect.width > 0 && rect.height > 0) {
      surface.drawRect(rect.x, rect.y, rect.width, rect.height, color);
    }
  }

  function radiusOption(options) {
    return optionValue(options, "radius", optionValue(options, "borderRadius", 0)) | 0;
  }

  function fillBox(context, rect, color, options) {
    var radius = radiusOption(options);
    var target = drawTarget(context);

    if (rect.width <= 0 || rect.height <= 0) {
      return;
    }
    if (radius > 0 && typeof target.fillRoundRect === "function") {
      target.fillRoundRect(rect.x, rect.y, rect.width, rect.height, radius, color);
    } else {
      fillRect(target, rect, color);
    }
  }

  function drawBoxBorder(context, rect, color, options) {
    var radius = radiusOption(options);
    var target = drawTarget(context);

    if (rect.width <= 0 || rect.height <= 0) {
      return;
    }
    if (radius > 0 && typeof target.drawRoundRect === "function") {
      target.drawRoundRect(rect.x, rect.y, rect.width, rect.height, radius, color);
    } else {
      drawBorder(target, rect, color);
    }
  }

  function drawInsetBorder(context, rect, color, options, inset) {
    var radius = radiusOption(options) - inset;
    var target = drawTarget(context);

    if (rect.width <= 0 || rect.height <= 0) {
      return;
    }
    if (radius > 0 && typeof target.drawRoundRect === "function") {
      target.drawRoundRect(rect.x, rect.y, rect.width, rect.height, radius, color);
    } else {
      drawBorder(target, rect, color);
    }
  }

  function drawFocusOutline(context, rect, options, control) {
    var color;
    var width;
    var i;
    var insetWidth;
    var insetHeight;

    if (!control.focused || !context.focusVisible) {
      return;
    }
    options = options || emptyOptions;
    if (hasOption(options, "outline") && !optionValue(options, "outline", true)) {
      return;
    }
    width = optionValue(options, "outlineWidth", 2) | 0;
    if (width <= 0 || rect.width <= 0 || rect.height <= 0) {
      return;
    }
    color = hasOption(options, "outlineColor")
      ? packedColor(context.surface, optionValue(options, "outlineColor", context.theme.focus), context.theme.focus, "ui outlineColor")
      : context.theme.focus;
    for (i = 0; i < width; i += 1) {
      insetWidth = rect.width - i * 2;
      insetHeight = rect.height - i * 2;
      if (insetWidth <= 0 || insetHeight <= 0) {
        break;
      }
      drawInsetBorder(context, {
        x: rect.x + i,
        y: rect.y + i,
        width: insetWidth,
        height: insetHeight
      }, color, options, i);
    }
  }

  function drawTextInRect(context, rect, text, options) {
    var surface = context.surface;
    var target = drawTarget(context);
    var padding = normalizeInsets(optionValue(options, "padding", 0));
    var align = optionValue(options, "align", "start");
    var valign = optionValue(options, "valign", "center");
    var metrics = measureText(context, text, options);
    var x = rect.x + padding.left;
    var y = rect.y + padding.top;
    var availableWidth = rect.width - padding.left - padding.right;
    var availableHeight = rect.height - padding.top - padding.bottom;
    var color = packedColor(surface, hasOption(options, "color") ? optionValue(options, "color", null) : null,
      context.theme.foreground, "ui text color");
    var style = {
      color: color,
      spacing: optionValue(options, "spacing", 0)
    };

    if (optionValue(options, "font", null)) {
      style.font = optionValue(options, "font", null);
    }
    if (align === "center") {
      x += alignOffset("center", availableWidth - metrics.width);
    } else if (align === "end") {
      x += alignOffset("end", availableWidth - metrics.width);
    }
    if (valign === "center") {
      y += alignOffset("center", availableHeight - metrics.height);
    } else if (valign === "end") {
      y += alignOffset("end", availableHeight - metrics.height);
    }
    target.drawText(x, y, String(text), style);
  }

  function drawDecoration(context, rect, options, fallbackBackground) {
    var surface = context.surface;
    var background;
    var border;

    options = options || emptyOptions;
    background = hasOption(options, "background")
      ? packedColor(surface, optionValue(options, "background", fallbackBackground), fallbackBackground, "ui background")
      : fallbackBackground;
    fillBox(context, rect, background, options);
    if (hasOption(options, "border") ? optionValue(options, "border", false) : false) {
      border = hasOption(options, "borderColor")
        ? packedColor(surface, optionValue(options, "borderColor", context.theme.border), context.theme.border, "ui borderColor")
        : context.theme.border;
      drawBoxBorder(context, rect, border, options);
    }
  }

  function defaultControlHeight(context, options) {
    var padding = normalizeInsets(optionValue(options, "padding", { x: 4, y: 3 }));
    var metrics = measureText(context, "M", options || emptyOptions);

    return metrics.height + padding.top + padding.bottom;
  }

  function defaultControlWidth(context, label, options) {
    var padding = normalizeInsets(optionValue(options, "padding", { x: 6, y: 3 }));
    var metrics = measureText(context, label, options || emptyOptions);

    return metrics.width + padding.left + padding.right;
  }

  function openLayout(type, options, defaultHeight, drawPanel) {
    var context = currentContext();
    var padding;
    var defaultSize = {};
    var rect;
    var content;
    var key;
    var control;
    var background;
    var border;
    var layoutBackground;

    options = options || emptyOptions;
    padding = normalizeInsets(optionValue(options, "padding", 0));
    if (own(options, "width")) {
      defaultSize.width = options.width | 0;
    }
    if (own(options, "height")) {
      defaultSize.height = options.height | 0;
    } else if (typeof defaultHeight === "number") {
      defaultSize.height = defaultHeight | 0;
    }
    rect = allocateRect(context, options, defaultSize);
    content = {
      x: rect.x + padding.left,
      y: rect.y + padding.top,
      width: rect.width - padding.left - padding.right,
      height: rect.height - padding.top - padding.bottom
    };
    if (content.width < 0) {
      content.width = 0;
    }
    if (content.height < 0) {
      content.height = 0;
    }

    if (drawPanel) {
      key = own(options, "id") ? String(options.id) : null;
      control = beginControl(context, type, key, rect, type + "|" + optionSignature(options), false);
      background = hasOption(options, "background")
        ? packedColor(context.surface, optionValue(options, "background", context.theme.panel), context.theme.panel, "ui panel background")
        : context.theme.panel;
      if (control.dirty) {
        border = hasOption(options, "border") ? optionValue(options, "border", true) : true;
        drawDecoration(context, rect, {
          background: background,
          border: border,
          borderColor: optionValue(options, "borderColor", context.theme.border),
          radius: optionValue(options, "radius", undefined),
          borderRadius: optionValue(options, "borderRadius", undefined)
        }, background);
      }
    }
    layoutBackground = drawPanel ? background : currentBackground(context);

    context.stack.push({
      type: type,
      rect: rect,
      content: content,
      cursorX: content.x,
      cursorY: content.y,
      gap: own(options, "gap") ? options.gap | 0 : 0,
      align: own(options, "align") ? options.align : "start",
      background: layoutBackground
    });
    return rect;
  }

  function resolveLayoutArgs(apiName, first, second) {
    var options = emptyOptions;
    var render;

    if (typeof first === "function") {
      render = first;
      options = second || emptyOptions;
    } else {
      options = first || emptyOptions;
      render = second;
    }
    if (options !== emptyOptions && (!options || typeof options !== "object")) {
      throw new Error(apiName + "(render?, options?) expects options to be an object");
    }
    if (render !== undefined && typeof render !== "function") {
      throw new Error(apiName + "(render?, options?) expects render to be a function");
    }
    return {
      options: options,
      render: render
    };
  }

  function runLayout(apiName, type, options, defaultHeight, drawPanel, render) {
    var context = currentContext();
    var baseDepth = context.stack.length;
    var snapshot = context.stack.slice(0, baseDepth);
    var rect = openLayout(type, options, defaultHeight, drawPanel);
    var callbackDepth = -1;

    if (typeof render !== "function") {
      return rect;
    }
    try {
      render(rect, context);
      callbackDepth = context.stack.length;
    } finally {
      context.stack = snapshot;
    }
    if (callbackDepth >= 0 && callbackDepth < baseDepth) {
      throw new Error(apiName + "(..., render) closed an outer layout");
    }
    if (callbackDepth > baseDepth + 1) {
      throw new Error(apiName + "(..., render) returned with open nested layouts");
    }
    return rect;
  }

  function activateControl(context, control) {
    return !!(control.focused && context.edges.ok) || control.touched;
  }

  function drawButtonLike(context, rect, label, options, control, selected) {
    var background;
    var foreground;
    var border;

    options = options || emptyOptions;
    background = selected ? context.theme.accent : context.theme.control;
    foreground = selected ? context.theme.accentText : context.theme.foreground;
    border = context.theme.border;
    if (activateControl(context, control)) {
      background = context.theme.pressed;
    }
    if (hasOption(options, "background")) {
      background = packedColor(context.surface, optionValue(options, "background", background), background, "ui button background");
    }
    if (hasOption(options, "color")) {
      foreground = packedColor(context.surface, optionValue(options, "color", foreground), foreground, "ui button color");
    }
    fillBox(context, rect, background, options);
    drawBoxBorder(context, rect, border, options);
    drawTextInRect(context, rect, label, {
      color: foreground,
      font: optionValue(options, "font", undefined),
      spacing: optionValue(options, "spacing", 0),
      padding: optionValue(options, "padding", { x: 5, y: 2 }),
      align: optionValue(options, "textAlign", "center")
    });
    drawFocusOutline(context, rect, options, control);
  }

  function numberOption(options, key, fallback) {
    if (options && own(options, key)) {
      return +options[key];
    }
    return fallback;
  }

  function clampNumber(value, minValue, maxValue) {
    if (value < minValue) {
      return minValue;
    }
    if (value > maxValue) {
      return maxValue;
    }
    return value;
  }

  ui.VERSION = "0.3.1";
  ui.theme = /** @type {ESP32QJS.UIModule["theme"]} */ (ui.theme || {});
  ui.theme.dark = rgbTheme();
  ui.theme.mono = monoTheme();

  ui.input = function (input) {
    if (input !== undefined) {
      pendingInput = normalizeInput(input);
    }
    return pendingInput;
  };

  ui.begin = function (surface, options) {
    var context;
    var frame;
    var root;
    var clear;
    var clearColor;

    if (!surface || typeof surface !== "object" ||
        typeof surface.present !== "function" ||
        typeof surface.supports !== "function") {
      throw new Error("ui.begin(display) expects an open display.Display");
    }
    if (activeContext) {
      throw new Error("ui.endFrame() must be called before starting another UI frame");
    }
    options = options || emptyOptions;
    context = findContext(surface) || createContext(surface);
    context.surface = surface;
    context.previousFocusOrder = context.focusOrder || [];
    context.focusOrder = [];
    context.nextAutoId = 0;
    context.dirty = null;
    if (context.dirtyRects) {
      context.dirtyRects.length = 0;
    } else {
      context.dirtyRects = [];
    }
    context.forceFull = false;
    context.partial = options.partial !== false;
    context.flush = options.flush !== false;
    context.dirtyFlushOptions = dirtyFlushOptions(surface, options);
    context.gcBeforeFlush = options.gcBeforeFlush !== false;
    context.draw = options.batch === false || !surface.supports("batch")
      ? surface
      : (surface.beginBatch(options.batch) || surface);
    context.theme = resolveTheme(surface, options.theme);
    context.input = normalizeInput(own(options, "input") ? options.input : pendingInput);
    context.edges = inputEdges(context.input, context.prevInput);
    if (own(options, "focusVisible")) {
      setFocusVisible(context, !!options.focusVisible);
    } else if (inputRevealsFocus(context.edges)) {
      setFocusVisible(context, true);
    }
    frame = rectFrom(surface, options, { x: 0, y: 0, width: surface.width, height: surface.height });
    context.rootFrame = frame;
    resolveMetrics(context, surface, options);
    clear = own(options, "clear") ? !!options.clear : !context.started;
    if (clear) {
      clearColor = own(options, "clearColor")
        ? packedColor(surface, options.clearColor, context.theme.background, "ui clearColor")
        : context.theme.background;
      context.draw.clear(clearColor);
      context.forceFull = true;
      markDirty(context, frame);
    }
    root = {
      type: "root",
      rect: frame,
      content: frame,
      cursorX: frame.x,
      cursorY: frame.y,
      gap: own(options, "gap") ? options.gap | 0 : 0,
      align: own(options, "align") ? options.align : "start",
      background: context.theme.background
    };
    context.stack = [root];
    updateFocusFromInput(context);
    activeContext = context;
    return context;
  };

  ui.endFrame = function () {
    var context = currentContext();
    var dirty;
    var dirtyRects;
    var flushRects;
    var surface;
    var dirtyArea;
    var screenArea;

    dirty = context.dirty;
    dirtyRects = context.dirtyRects || [];
    surface = context.surface;
    if (context.stack.length !== 1) {
      activeContext = null;
      throw new Error("ui.end() is missing for an open layout");
    }
    if (context.draw !== surface) {
      surface.endBatch(context.draw, dirty);
    }
    if (context.flush && dirty) {
      dirtyArea = dirty.width * dirty.height;
      screenArea = surface.width * surface.height;
      if (context.gcBeforeFlush &&
          (context.forceFull || dirtyArea >= (screenArea >> 1)) &&
          typeof global.gc === "function") {
        global.gc();
      }
      if (context.partial && surface.supports("partialPresent")) {
        flushRects = resolveDirtyFlushRects(context, dirty, dirtyRects);
        surface.present(flushRects, {
          merge: false
        });
      } else {
        surface.flush();
      }
    }
    context.prevInput = context.input;
    context.started = true;
    activeContext = null;
    return context;
  };

  ui.frame = function (surface, render, options) {
    var context;

    if (typeof render !== "function") {
      throw new Error("ui.frame(surface, render) expects a render function");
    }
    context = ui.begin(surface, options);
    try {
      render(context);
    } catch (error) {
      activeContext = null;
      throw error;
    }
    return ui.endFrame();
  };

  ui.row = function (renderOrOptions, options) {
    var args = resolveLayoutArgs("ui.row", renderOrOptions, options);

    return runLayout("ui.row", "row", args.options,
      own(args.options, "height") ? undefined : defaultControlHeight(currentContext(), args.options),
      false,
      args.render);
  };

  ui.column = function (renderOrOptions, options) {
    var args = resolveLayoutArgs("ui.column", renderOrOptions, options);

    return runLayout("ui.column", "column", args.options, undefined, false, args.render);
  };

  ui.group = function (renderOrOptions, options) {
    var args = resolveLayoutArgs("ui.group", renderOrOptions, options);

    return runLayout("ui.group", "column", args.options, undefined, false, args.render);
  };

  ui.panel = function (renderOrOptions, options) {
    var args = resolveLayoutArgs("ui.panel", renderOrOptions, options);

    return runLayout("ui.panel", "column", args.options, undefined, true, args.render);
  };

  ui.end = function () {
    var context = currentContext();

    if (context.stack.length <= 1) {
      throw new Error("ui.end() has no open layout");
    }
    return context.stack.pop().rect;
  };

  ui.spacer = function (sizeOrOptions) {
    var context = currentContext();
    var options = /** @type {ESP32QJS.UISpacerOptions} */ ({});
    var rect;

    if (typeof sizeOrOptions === "number") {
      options.height = sizeOrOptions | 0;
      options.width = sizeOrOptions | 0;
    } else {
      options = sizeOrOptions || {};
    }
    rect = allocateRect(context, options, {
      width: own(options, "width") ? options.width | 0 : 0,
      height: own(options, "height") ? options.height | 0 : (own(options, "size") ? options.size | 0 : 0)
    });
    return rect;
  };

  ui.separator = function (options) {
    var context = currentContext();
    var horizontal = topLayout(context).type !== "row";
    var thickness;
    var rect;
    var control;

    options = options || emptyOptions;
    thickness = own(options, "thickness") ? options.thickness | 0 : 1;
    rect = allocateRect(context, options, horizontal
      ? { width: topLayout(context).content.width, height: thickness }
      : { width: thickness, height: topLayout(context).content.height });
    control = beginControl(context, "separator", own(options, "id") ? String(options.id) : null,
      rect, "separator|" + optionSignature(options), false);
    if (control.dirty) {
      fillRect(drawTarget(context), rect, hasOption(options, "color")
        ? packedColor(context.surface, optionValue(options, "color", context.theme.border), context.theme.border, "ui separator color")
        : context.theme.border);
    }
    return rect;
  };

  ui.text = function (value, options) {
    var context = currentContext();
    var text = String(value);
    var metrics;
    var padding;
    var hasWidth;
    var hasHeight;
    var rect;
    var control;
    var background;

    options = options || emptyOptions;
    hasWidth = own(options, "width");
    hasHeight = own(options, "height");
    if (!hasWidth || !hasHeight) {
      metrics = measureText(context, text, options);
      padding = normalizeInsets(optionValue(options, "padding", 0));
    } else {
      metrics = { width: 0, height: 0 };
      padding = { top: 0, right: 0, bottom: 0, left: 0 };
    }
    rect = allocateRect(context, options, {
      width: hasWidth ? options.width | 0 : metrics.width + padding.left + padding.right,
      height: hasHeight ? options.height | 0 : metrics.height + padding.top + padding.bottom
    });
    control = beginControl(context, "text", own(options, "id") ? String(options.id) : null,
      rect, "text|" + text + "|" + optionSignature(options), false);
    if (control.dirty) {
      background = hasOption(options, "background")
        ? packedColor(context.surface, optionValue(options, "background", currentBackground(context)), currentBackground(context), "ui text background")
        : currentBackground(context);
      fillBox(context, rect, background, options);
      drawTextInRect(context, rect, text, options);
    }
    return rect;
  };

  ui.value = function (label, value, options) {
    var text = String(label) + ": " + String(value);

    options = options || emptyOptions;
    if (!hasOption(options, "font") && own(options, "valueFont")) {
      options = copyObject(options);
      options.font = options.valueFont;
    }
    return ui.text(text, options);
  };

  ui.badge = function (text, options) {
    var context = currentContext();
    var label = String(text);
    var rect;
    var control;

    options = options || emptyOptions;
    rect = allocateRect(context, options, {
      width: own(options, "width") ? options.width | 0 : defaultControlWidth(context, label, options),
      height: own(options, "height") ? options.height | 0 : defaultControlHeight(context, options)
    });
    control = beginControl(context, "badge", own(options, "id") ? String(options.id) : null,
      rect, "badge|" + label + "|" + optionSignature(options), false);
    if (control.dirty) {
      drawButtonLike(context, rect, label, options, control, !!options.selected);
    }
    return rect;
  };

  ui.button = function (id, label, options) {
    var context = currentContext();
    var rect;
    var control;
    var activated;

    options = options || emptyOptions;
    rect = allocateRect(context, options, {
      width: defaultControlWidth(context, label, options),
      height: defaultControlHeight(context, options)
    });
    control = beginControl(context, "button", String(id), rect,
      "button|" + String(label) + "|" + optionSignature(options), true);
    activated = activateControl(context, control);
    if (control.dirty || activated) {
      drawButtonLike(context, rect, String(label), options, control, false);
    }
    return activated;
  };

  ui.icon = function (name, options) {
    options = options || emptyOptions;
    if (!own(options, "align")) {
      options = copyObject(options);
      options.align = "center";
    }
    return ui.text(String(name).slice(0, 2), options);
  };

  ui.iconButton = function (id, icon, options) {
    var label;

    options = options || emptyOptions;
    label = own(options, "label") ? String(options.label) : String(icon);
    return ui.button(id, label, options);
  };

  ui.toggle = function (id, label, value, options) {
    var context = currentContext();
    var next = !!value;
    var rect;
    var control;
    var activated;
    var box;
    var knob;
    var textRect;

    options = options || emptyOptions;
    rect = allocateRect(context, options, {
      width: own(options, "width") ? options.width | 0 : topLayout(context).content.width,
      height: defaultControlHeight(context, options) + 2
    });
    control = beginControl(context, "toggle", String(id), rect,
      "toggle|" + String(label) + "|" + (next ? "1" : "0") + "|" + optionSignature(options), true);
    activated = activateControl(context, control);
    if (activated) {
      next = !next;
      markDirty(context, rect);
      control.state.signature = "";
    }
    if (control.dirty || activated) {
      fillBox(context, rect, currentBackground(context), options);
      textRect = { x: rect.x, y: rect.y, width: rect.width - 26, height: rect.height };
      drawTextInRect(context, textRect, label, {
        color: context.theme.foreground,
        font: optionValue(options, "font", undefined),
        padding: optionValue(options, "padding", { x: 2, y: 0 }),
        align: "start"
      });
      box = { x: rect.x + rect.width - 24, y: rect.y + ((rect.height - 12) >> 1), width: 22, height: 12 };
      fillRect(drawTarget(context), box, next ? context.theme.accent : context.theme.control);
      drawBorder(drawTarget(context), box, context.theme.border);
      knob = {
        x: box.x + (next ? 11 : 1),
        y: box.y + 1,
        width: 10,
        height: 10
      };
      fillRect(drawTarget(context), knob, next ? context.theme.accentText : context.theme.foreground);
      drawFocusOutline(context, rect, options, control);
    }
    return next;
  };

  ui.checkbox = function (id, label, value, options) {
    var context = currentContext();
    var next = !!value;
    var rect;
    var control;
    var activated;
    var box;
    var textRect;

    options = options || emptyOptions;
    rect = allocateRect(context, options, {
      width: own(options, "width") ? options.width | 0 : topLayout(context).content.width,
      height: defaultControlHeight(context, options) + 2
    });
    control = beginControl(context, "checkbox", String(id), rect,
      "checkbox|" + String(label) + "|" + (next ? "1" : "0") + "|" + optionSignature(options), true);
    activated = activateControl(context, control);
    if (activated) {
      next = !next;
      markDirty(context, rect);
      control.state.signature = "";
    }
    if (control.dirty || activated) {
      fillBox(context, rect, currentBackground(context), options);
      box = { x: rect.x + 2, y: rect.y + ((rect.height - 10) >> 1), width: 10, height: 10 };
      fillRect(drawTarget(context), box, next ? context.theme.accent : context.theme.control);
      drawBorder(drawTarget(context), box, context.theme.border);
      if (next) {
        drawTarget(context).drawLine(box.x + 2, box.y + 5, box.x + 4, box.y + 8, context.theme.accentText);
        drawTarget(context).drawLine(box.x + 4, box.y + 8, box.x + 8, box.y + 2, context.theme.accentText);
      }
      textRect = { x: rect.x + 16, y: rect.y, width: rect.width - 16, height: rect.height };
      drawTextInRect(context, textRect, label, {
        color: context.theme.foreground,
        font: optionValue(options, "font", undefined),
        padding: optionValue(options, "padding", { x: 2, y: 0 }),
        align: "start"
      });
      drawFocusOutline(context, rect, options, control);
    }
    return next;
  };

  ui.progress = function (id, value, options) {
    var context = currentContext();
    var minValue;
    var maxValue;
    var current;
    var ratio;
    var rect;
    var fill;
    var control;

    options = options || emptyOptions;
    minValue = numberOption(options, "min", 0);
    maxValue = numberOption(options, "max", 100);
    current = clampNumber(+value, minValue, maxValue);
    ratio = maxValue === minValue ? 0 : (current - minValue) / (maxValue - minValue);
    rect = allocateRect(context, options, {
      width: own(options, "width") ? options.width | 0 : topLayout(context).content.width,
      height: own(options, "height") ? options.height | 0 : 10
    });
    control = beginControl(context, "progress", String(id), rect,
      "progress|" + current + "|" + optionSignature(options, ["min", "max"]), false);
    if (control.dirty) {
      fillBox(context, rect, hasOption(options, "background")
        ? packedColor(context.surface, optionValue(options, "background", context.theme.control), context.theme.control, "ui progress background")
        : context.theme.control, options);
      drawBoxBorder(context, rect, context.theme.border, options);
      fill = {
        x: rect.x + 1,
        y: rect.y + 1,
        width: ((rect.width - 2) * ratio) | 0,
        height: rect.height - 2
      };
      if (fill.width > 0 && fill.height > 0) {
        fillRect(drawTarget(context), fill, hasOption(options, "color")
          ? packedColor(context.surface, optionValue(options, "color", context.theme.accent), context.theme.accent, "ui progress color")
          : context.theme.accent);
      }
    }
    return rect;
  };

  ui.gauge = function (id, value, options) {
    options = options || emptyOptions;
    if (!own(options, "height")) {
      options = copyObject(options);
      options.height = 14;
    }
    return ui.progress(id, value, options);
  };

  ui.slider = function (id, value, options) {
    var context = currentContext();
    var minValue;
    var maxValue;
    var step;
    var next;
    var ratio;
    var rect;
    var control;
    var track;
    var knobX;

    options = options || emptyOptions;
    minValue = numberOption(options, "min", 0);
    maxValue = numberOption(options, "max", 100);
    step = numberOption(options, "step", 1);
    next = clampNumber(+value, minValue, maxValue);
    rect = allocateRect(context, options, {
      width: own(options, "width") ? options.width | 0 : topLayout(context).content.width,
      height: own(options, "height") ? options.height | 0 : 18
    });
    control = beginControl(context, "slider", String(id), rect,
      "slider|" + next + "|" + optionSignature(options, ["min", "max", "step"]), true);
    if (control.focused) {
      if (context.edges.left || context.edges.encoderDelta < 0) {
        next = clampNumber(next - step, minValue, maxValue);
      }
      if (context.edges.right || context.edges.encoderDelta > 0) {
        next = clampNumber(next + step, minValue, maxValue);
      }
    }
    if (next !== value) {
      markDirty(context, rect);
      control.state.signature = "";
    }
    ratio = maxValue === minValue ? 0 : (next - minValue) / (maxValue - minValue);
    if (control.dirty || next !== value) {
      fillBox(context, rect, currentBackground(context), options);
      track = { x: rect.x + 4, y: rect.y + ((rect.height - 4) >> 1), width: rect.width - 8, height: 4 };
      fillRect(drawTarget(context), track, context.theme.control);
      drawBoxBorder(context, rect, context.theme.border, options);
      knobX = track.x + (((track.width - 4) * ratio) | 0);
      fillRect(drawTarget(context), { x: knobX, y: rect.y + 2, width: 6, height: rect.height - 4 }, context.theme.accent);
      drawFocusOutline(context, rect, options, control);
    }
    return next;
  };

  ui.stepper = function (id, value, options) {
    var context = currentContext();
    var minValue;
    var maxValue;
    var step;
    var next;
    var label;
    var rect;
    var control;

    options = options || emptyOptions;
    minValue = numberOption(options, "min", -2147483648);
    maxValue = numberOption(options, "max", 2147483647);
    step = numberOption(options, "step", 1);
    next = clampNumber(+value, minValue, maxValue);
    label = (own(options, "label") ? String(options.label) + " " : "") + "< " + String(next) + " >";
    rect = allocateRect(context, options, {
      width: own(options, "width") ? options.width | 0 : topLayout(context).content.width,
      height: defaultControlHeight(context, options)
    });
    control = beginControl(context, "stepper", String(id), rect,
      "stepper|" + label + "|" + optionSignature(options), true);
    if (control.focused) {
      if (context.edges.left || context.edges.encoderDelta < 0) {
        next = clampNumber(next - step, minValue, maxValue);
      }
      if (context.edges.right || context.edges.encoderDelta > 0) {
        next = clampNumber(next + step, minValue, maxValue);
      }
    }
    if (next !== value) {
      markDirty(context, rect);
      control.state.signature = "";
      label = (own(options, "label") ? String(options.label) + " " : "") + "< " + String(next) + " >";
    }
    if (control.dirty || next !== value) {
      drawButtonLike(context, rect, label, options, control, false);
    }
    return next;
  };

  ui.list = function (id, items, selectedIndex, options) {
    var context = currentContext();
    var list = items || [];
    var next = clampInt(selectedIndex | 0, 0, list.length > 0 ? list.length - 1 : 0);
    var visibleCount;
    var rowHeight;
    var rect;
    var control;
    var start;
    var i;
    var itemIndex;
    var row;
    var selected;

    options = options || emptyOptions;
    visibleCount = own(options, "visibleCount") ? options.visibleCount | 0 : (list.length < 4 ? list.length : 4);
    if (visibleCount <= 0) {
      visibleCount = 1;
    }
    rowHeight = own(options, "rowHeight") ? options.rowHeight | 0 : defaultControlHeight(context, options);
    rect = allocateRect(context, options, {
      width: own(options, "width") ? options.width | 0 : topLayout(context).content.width,
      height: own(options, "height") ? options.height | 0 : rowHeight * visibleCount
    });
    control = beginControl(context, "list", String(id), rect,
      "list|" + next + "|" + listSignature(list) + "|" + optionSignature(options, ["visibleCount", "rowHeight"]), true);
    if (control.focused) {
      if (context.edges.left || context.edges.encoderDelta < 0) {
        next = clampInt(next - 1, 0, list.length > 0 ? list.length - 1 : 0);
      }
      if (context.edges.right || context.edges.encoderDelta > 0) {
        next = clampInt(next + 1, 0, list.length > 0 ? list.length - 1 : 0);
      }
    }
    if (next !== selectedIndex) {
      markDirty(context, rect);
      control.state.signature = "";
    }
    if (control.dirty || next !== selectedIndex) {
      fillBox(context, rect, currentBackground(context), options);
      drawBoxBorder(context, rect, context.theme.border, options);
      start = next - ((visibleCount / 2) | 0);
      if (start < 0) {
        start = 0;
      }
      if (start + visibleCount > list.length) {
        start = list.length - visibleCount;
      }
      if (start < 0) {
        start = 0;
      }
      for (i = 0; i < visibleCount; i += 1) {
        itemIndex = start + i;
        if (itemIndex >= list.length) {
          break;
        }
        row = { x: rect.x + 1, y: rect.y + 1 + i * rowHeight, width: rect.width - 2, height: rowHeight };
        selected = itemIndex === next;
        fillRect(drawTarget(context), row, selected ? context.theme.accent : currentBackground(context));
        drawTextInRect(context, row, list[itemIndex], {
          color: selected ? context.theme.accentText : context.theme.foreground,
          font: optionValue(options, "font", undefined),
          padding: { x: 3, y: 0 },
          align: "start"
        });
      }
      drawFocusOutline(context, rect, options, control);
    }
    return next;
  };

  ui.menu = function (id, items, selectedIndex, options) {
    return ui.list(id, items, selectedIndex, options);
  };

  ui.tabs = function (id, tabs, selectedIndex, options) {
    var context = currentContext();
    var list = tabs || [];
    var next = clampInt(selectedIndex | 0, 0, list.length > 0 ? list.length - 1 : 0);
    var rect;
    var control;
    var i;
    var tabWidth;
    var tabRect;
    var selected;

    options = options || emptyOptions;
    rect = allocateRect(context, options, {
      width: own(options, "width") ? options.width | 0 : topLayout(context).content.width,
      height: own(options, "height") ? options.height | 0 : defaultControlHeight(context, options)
    });
    control = beginControl(context, "tabs", String(id), rect,
      "tabs|" + next + "|" + listSignature(list) + "|" + optionSignature(options), true);
    if (control.focused) {
      if (context.edges.left || context.edges.encoderDelta < 0) {
        next = clampInt(next - 1, 0, list.length > 0 ? list.length - 1 : 0);
      }
      if (context.edges.right || context.edges.encoderDelta > 0) {
        next = clampInt(next + 1, 0, list.length > 0 ? list.length - 1 : 0);
      }
    }
    if (next !== selectedIndex) {
      markDirty(context, rect);
      control.state.signature = "";
    }
    if (control.dirty || next !== selectedIndex) {
      fillBox(context, rect, currentBackground(context), options);
      drawBoxBorder(context, rect, context.theme.border, options);
      tabWidth = list.length > 0 ? (rect.width / list.length) | 0 : rect.width;
      for (i = 0; i < list.length; i += 1) {
        tabRect = {
          x: rect.x + i * tabWidth,
          y: rect.y,
          width: i === list.length - 1 ? rect.x + rect.width - (rect.x + i * tabWidth) : tabWidth,
          height: rect.height
        };
        selected = i === next;
        fillRect(drawTarget(context), tabRect, selected ? context.theme.accent : context.theme.control);
        drawBorder(drawTarget(context), tabRect, context.theme.border);
        drawTextInRect(context, tabRect, list[i], {
          color: selected ? context.theme.accentText : context.theme.foreground,
          font: optionValue(options, "font", undefined),
          padding: { x: 2, y: 0 },
          align: "center"
        });
      }
      drawFocusOutline(context, rect, options, control);
    }
    return next;
  };

  ui.softkeys = function (left, center, right, options) {
    var context = currentContext();
    var rect;
    var control;
    var labels;
    var i;
    var keyRect;
    var width;
    var result = null;

    options = options || emptyOptions;
    labels = [left || "", center || "", right || ""];
    rect = allocateRect(context, options, {
      width: own(options, "width") ? options.width | 0 : topLayout(context).content.width,
      height: own(options, "height") ? options.height | 0 : defaultControlHeight(context, options)
    });
    control = beginControl(context, "softkeys", own(options, "id") ? String(options.id) : null,
      rect, "softkeys|" + listSignature(labels) + "|" + optionSignature(options), true);
    if (control.focused && context.edges.back) {
      result = "left";
    } else if (control.focused && context.edges.ok) {
      result = "center";
    } else if (control.focused && context.edges.right) {
      result = "right";
    }
    if (control.dirty || result) {
      fillBox(context, rect, context.theme.control, options);
      drawBoxBorder(context, rect, context.theme.border, options);
      width = (rect.width / 3) | 0;
      for (i = 0; i < 3; i += 1) {
        keyRect = {
          x: rect.x + i * width,
          y: rect.y,
          width: i === 2 ? rect.x + rect.width - (rect.x + i * width) : width,
          height: rect.height
        };
        drawTextInRect(context, keyRect, labels[i], {
          color: context.theme.foreground,
          font: optionValue(options, "font", undefined),
          padding: { x: 2, y: 0 },
          align: i === 0 ? "start" : (i === 1 ? "center" : "end")
        });
      }
      drawFocusOutline(context, rect, options, control);
    }
    return result;
  };

  ui.statusBar = function (options) {
    var context = currentContext();
    var rect;
    var control;
    var left;
    var center;
    var right;
    var third;

    options = options || emptyOptions;
    left = own(options, "left") ? String(options.left) : "";
    center = own(options, "title") ? String(options.title) : (own(options, "center") ? String(options.center) : "");
    right = own(options, "right") ? String(options.right) : "";
    rect = allocateRect(context, options, {
      width: own(options, "width") ? options.width | 0 : topLayout(context).content.width,
      height: own(options, "height") ? options.height | 0 : defaultControlHeight(context, options)
    });
    control = beginControl(context, "statusBar", own(options, "id") ? String(options.id) : null,
      rect, "status|" + left + "|" + center + "|" + right + "|" + optionSignature(options), false);
    if (control.dirty) {
      fillBox(context, rect, hasOption(options, "background")
        ? packedColor(context.surface, optionValue(options, "background", context.theme.control), context.theme.control, "ui statusBar background")
        : context.theme.control, options);
      third = (rect.width / 3) | 0;
      drawTextInRect(context, { x: rect.x, y: rect.y, width: third, height: rect.height }, left, {
        color: context.theme.foreground,
        font: optionValue(options, "font", undefined),
        padding: { x: 2, y: 0 },
        align: "start"
      });
      drawTextInRect(context, { x: rect.x + third, y: rect.y, width: third, height: rect.height }, center, {
        color: context.theme.foreground,
        font: optionValue(options, "font", undefined),
        padding: { x: 2, y: 0 },
        align: "center"
      });
      drawTextInRect(context, { x: rect.x + third * 2, y: rect.y, width: rect.width - third * 2, height: rect.height }, right, {
        color: context.theme.foreground,
        font: optionValue(options, "font", undefined),
        padding: { x: 2, y: 0 },
        align: "end"
      });
    }
    return rect;
  };

  ui.__loaded = true;
  global.ui = ui;
})(globalThis);
