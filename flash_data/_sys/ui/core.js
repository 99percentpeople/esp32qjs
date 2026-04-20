(function (global) {
  var owns = Object.prototype.hasOwnProperty;
  var display = global.display;
  var ui = global.ui || {};

  if (ui.__loaded) {
    return;
  }
  if (!display || !display.__loaded) {
    throw new Error("load(\"_sys/display.js\") before load(\"_sys/ui.js\")");
  }

  function own(obj, key) {
    return obj !== null && obj !== undefined && owns.call(obj, key);
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

  function toColor(value, fallback) {
    if (value === undefined) {
      return fallback;
    }
    return value === false || value === 0 || value === null ? 0 : 1;
  }

  function isArray(value) {
    return Object.prototype.toString.call(value) === "[object Array]";
  }

  function cloneObject(source) {
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

  function normalizeNode(value) {
    if (value === null || value === undefined || value === false) {
      return null;
    }
    if (typeof value === "string" || typeof value === "number" || typeof value === "boolean") {
      return createNode("text", { text: String(value) }, []);
    }
    if (!value || typeof value !== "object" || !own(value, "type")) {
      throw new Error("ui node must be a ui.*() node, string, number, or boolean");
    }
    return value;
  }

  function pushNode(target, value) {
    var i;
    var normalized;

    if (isArray(value)) {
      for (i = 0; i < value.length; i += 1) {
        pushNode(target, value[i]);
      }
      return;
    }
    normalized = normalizeNode(value);
    if (normalized) {
      target.push(normalized);
    }
  }

  function collectChildren(args, startIndex) {
    var children = [];
    var i;

    for (i = startIndex; i < args.length; i += 1) {
      pushNode(children, args[i]);
    }
    return children;
  }

  function createNode(type, props, children) {
    return {
      type: type,
      props: cloneObject(props),
      children: children || [],
      frame: null,
      contentFrame: null
    };
  }

  function parseNodeArgs(args, defaultType) {
    var props = {};
    var startIndex = 0;

    if (args.length > 0 && args[0] && typeof args[0] === "object" && !isArray(args[0]) && !own(args[0], "type")) {
      props = args[0];
      startIndex = 1;
    }

    return createNode(defaultType, props, collectChildren(args, startIndex));
  }

  function explicitSize(value) {
    if (typeof value === "number") {
      return value | 0;
    }
    return null;
  }

  function measureChildrenStack(surface, children) {
    var size = { width: 0, height: 0 };
    var i;
    var childSize;

    for (i = 0; i < children.length; i += 1) {
      childSize = measureNode(surface, children[i]);
      if (childSize.width > size.width) {
        size.width = childSize.width;
      }
      if (childSize.height > size.height) {
        size.height = childSize.height;
      }
    }
    return size;
  }

  function measureChildrenAxis(surface, children, isRow, gap) {
    var size = { width: 0, height: 0 };
    var i;
    var childSize;
    var main = 0;
    var cross = 0;

    for (i = 0; i < children.length; i += 1) {
      childSize = measureNode(surface, children[i]);
      if (isRow) {
        main += childSize.width;
        if (childSize.height > cross) {
          cross = childSize.height;
        }
      } else {
        main += childSize.height;
        if (childSize.width > cross) {
          cross = childSize.width;
        }
      }
    }

    if (children.length > 1) {
      main += gap * (children.length - 1);
    }

    if (isRow) {
      size.width = main;
      size.height = cross;
    } else {
      size.width = cross;
      size.height = main;
    }
    return size;
  }

  function measureNode(surface, node) {
    var props = node.props || {};
    var padding = normalizeInsets(props.padding);
    var innerWidth = 0;
    var innerHeight = 0;
    var textSize;
    var childrenSize;
    var width = explicitSize(props.width);
    var height = explicitSize(props.height);

    if (node.type === "text") {
      textSize = surface.measureText(props.text || "", { spacing: own(props, "spacing") ? props.spacing : 0 });
      innerWidth = textSize.width;
      innerHeight = textSize.height;
    } else if (node.type === "spacer") {
      innerWidth = explicitSize(props.width);
      innerHeight = explicitSize(props.height);
      if (innerWidth === null) {
        innerWidth = own(props, "size") ? props.size | 0 : 0;
      }
      if (innerHeight === null) {
        innerHeight = own(props, "size") ? props.size | 0 : 0;
      }
    } else if (node.type === "row") {
      childrenSize = measureChildrenAxis(surface, node.children, true, own(props, "gap") ? props.gap | 0 : 0);
      innerWidth = childrenSize.width;
      innerHeight = childrenSize.height;
    } else if (node.type === "column") {
      childrenSize = measureChildrenAxis(surface, node.children, false, own(props, "gap") ? props.gap | 0 : 0);
      innerWidth = childrenSize.width;
      innerHeight = childrenSize.height;
    } else {
      childrenSize = measureChildrenStack(surface, node.children);
      innerWidth = childrenSize.width;
      innerHeight = childrenSize.height;
    }

    return {
      width: width !== null ? width : innerWidth + padding.left + padding.right,
      height: height !== null ? height : innerHeight + padding.top + padding.bottom
    };
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

  function paintDecoration(surface, node) {
    var props = node.props || {};
    var frame = node.frame;
    var backgroundColor;
    var borderColor;

    if (!frame || frame.width <= 0 || frame.height <= 0) {
      return;
    }

    if (own(props, "background")) {
      backgroundColor = toColor(props.background, 0);
      surface.fillRect(frame.x, frame.y, frame.width, frame.height, backgroundColor);
    }

    if (own(props, "border") && props.border) {
      borderColor = own(props, "borderColor") ? toColor(props.borderColor, 1) : 1;
      surface.drawRect(frame.x, frame.y, frame.width, frame.height, borderColor);
    }
  }

  function layoutOverlay(surface, node) {
    var props = node.props || {};
    var content = node.contentFrame;
    var align = own(props, "align") ? props.align : "start";
    var valign = own(props, "valign") ? props.valign : "start";
    var i;
    var child;
    var size;
    var frame;
    var childWidth;
    var childHeight;

    for (i = 0; i < node.children.length; i += 1) {
      child = node.children[i];
      size = measureNode(surface, child);
      childWidth = size.width;
      childHeight = size.height;

      if (align === "stretch") {
        childWidth = content.width;
      }
      if (valign === "stretch") {
        childHeight = content.height;
      }

      frame = {
        x: content.x + alignOffset(align, content.width - childWidth),
        y: content.y + alignOffset(valign, content.height - childHeight),
        width: childWidth,
        height: childHeight
      };
      layoutNode(surface, child, frame);
    }
  }

  function layoutAxis(surface, node, isRow) {
    var props = node.props || {};
    var content = node.contentFrame;
    var gap = own(props, "gap") ? props.gap | 0 : 0;
    var justify = own(props, "justify") ? props.justify : "start";
    var align = own(props, "align") ? props.align : "start";
    var children = node.children;
    var sizes = [];
    var totalMain = 0;
    var totalFlex = 0;
    var mainLimit = isRow ? content.width : content.height;
    var crossLimit = isRow ? content.height : content.width;
    var availableExtra;
    var i;
    var child;
    var size;
    var childFlex;
    var mainStart = 0;
    var betweenGap = gap;
    var frame;
    var childMain;
    var childCross;
    var remaining;

    for (i = 0; i < children.length; i += 1) {
      child = children[i];
      size = measureNode(surface, child);
      sizes.push(size);
      totalMain += isRow ? size.width : size.height;
      childFlex = own(child.props, "flex") ? child.props.flex | 0 : 0;
      if (childFlex > 0) {
        totalFlex += childFlex;
      }
    }

    if (children.length > 1) {
      totalMain += gap * (children.length - 1);
    }

    availableExtra = mainLimit - totalMain;
    if (availableExtra < 0) {
      availableExtra = 0;
    }

    if (totalFlex === 0) {
      if (justify === "center") {
        mainStart = availableExtra >> 1;
      } else if (justify === "end") {
        mainStart = availableExtra;
      } else if (justify === "space-between" && children.length > 1) {
        betweenGap = gap + ((availableExtra / (children.length - 1)) | 0);
      }
    }

    remaining = availableExtra;
    for (i = 0; i < children.length; i += 1) {
      child = children[i];
      size = sizes[i];
      childFlex = own(child.props, "flex") ? child.props.flex | 0 : 0;
      childMain = isRow ? size.width : size.height;
      childCross = isRow ? size.height : size.width;

      if (totalFlex > 0 && childFlex > 0 && remaining > 0) {
        childMain += ((availableExtra * childFlex) / totalFlex) | 0;
      }

      if (align === "stretch") {
        childCross = crossLimit;
      }

      if (isRow) {
        frame = {
          x: content.x + mainStart,
          y: content.y + alignOffset(align, content.height - childCross),
          width: childMain,
          height: childCross
        };
      } else {
        frame = {
          x: content.x + alignOffset(align, content.width - childCross),
          y: content.y + mainStart,
          width: childCross,
          height: childMain
        };
      }

      layoutNode(surface, child, frame);
      mainStart += childMain + betweenGap;
    }
  }

  function layoutNode(surface, node, frame) {
    var props = node.props || {};
    var padding = normalizeInsets(props.padding);
    var width = clampInt(frame.width | 0, 0, surface.width);
    var height = clampInt(frame.height | 0, 0, surface.height);

    node.frame = {
      x: frame.x | 0,
      y: frame.y | 0,
      width: width,
      height: height
    };
    node.contentFrame = {
      x: node.frame.x + padding.left,
      y: node.frame.y + padding.top,
      width: width - padding.left - padding.right,
      height: height - padding.top - padding.bottom
    };

    if (node.contentFrame.width < 0) {
      node.contentFrame.width = 0;
    }
    if (node.contentFrame.height < 0) {
      node.contentFrame.height = 0;
    }

    if (node.type === "row") {
      layoutAxis(surface, node, true);
    } else if (node.type === "column") {
      layoutAxis(surface, node, false);
    } else if (node.type !== "text" && node.type !== "spacer") {
      layoutOverlay(surface, node);
    }
  }

  function paintNode(surface, node) {
    var props = node.props || {};
    var frame = node.frame;
    var children = node.children;
    var i;
    var color;

    paintDecoration(surface, node);

    if (!frame || frame.width <= 0 || frame.height <= 0) {
      return;
    }

    if (node.type === "text") {
      color = own(props, "color") ? toColor(props.color, 1) : 1;
      surface.drawText(frame.x, frame.y, props.text || "", color, own(props, "spacing") ? props.spacing : 0);
      return;
    }

    for (i = 0; i < children.length; i += 1) {
      paintNode(surface, children[i]);
    }
  }

  function defaultRenderFrame(surface, root, options) {
    var props = root.props || {};

    options = options || {};
    return {
      x: own(options, "x") ? options.x | 0 : 0,
      y: own(options, "y") ? options.y | 0 : 0,
      width: own(options, "width") ? options.width | 0 :
        (explicitSize(props.width) !== null ? props.width | 0 : surface.width),
      height: own(options, "height") ? options.height | 0 :
        (explicitSize(props.height) !== null ? props.height | 0 : surface.height)
    };
  }

  ui.VERSION = "0.1.0";
  ui.node = createNode;
  ui.box = function () {
    return parseNodeArgs(arguments, "box");
  };
  ui.row = function () {
    return parseNodeArgs(arguments, "row");
  };
  ui.column = function () {
    return parseNodeArgs(arguments, "column");
  };
  ui.text = function (value, props) {
    var textProps = cloneObject(props);

    textProps.text = String(value);
    return createNode("text", textProps, []);
  };
  ui.spacer = function (value) {
    var props = {};

    if (typeof value === "number") {
      props.size = value | 0;
    } else {
      props = cloneObject(value);
    }
    return createNode("spacer", props, []);
  };
  ui.padding = function (insets, child, props) {
    var nodeProps = cloneObject(props);

    nodeProps.padding = normalizeInsets(insets);
    return createNode("box", nodeProps, collectChildren([child], 0));
  };
  ui.measure = function (surface, node) {
    return measureNode(surface, normalizeNode(node));
  };
  ui.layout = function (surface, node, options) {
    var root = normalizeNode(node);
    var frame = defaultRenderFrame(surface, root, options);

    layoutNode(surface, root, frame);
    return root;
  };
  ui.render = function (surface, node, options) {
    var root;
    var settings = options || {};
    var clearColor = own(settings, "clearColor") ? toColor(settings.clearColor, 0) : 0;

    if (settings.clear !== false) {
      surface.clear(clearColor);
    }
    root = ui.layout(surface, node, settings);
    paintNode(surface, root);
    if (settings.flush !== false) {
      surface.flush();
    }
    return root;
  };
  ui.__loaded = true;

  global.ui = ui;
})(globalThis);
