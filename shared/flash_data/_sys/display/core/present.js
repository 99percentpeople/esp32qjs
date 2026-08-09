(function (global) {
  var system = global.__displaySystemV2;

  if (!system || system.presentLoaded) {
    return;
  }

  function own(value, key) {
    return system.own(value, key);
  }

  function normalizeRect(surface, value) {
    var x;
    var y;
    var width;
    var height;

    if (!value || typeof value !== "object") {
      return null;
    }
    if (typeof value.length === "number" && value.length >= 4) {
      x = value[0];
      y = value[1];
      width = value[2];
      height = value[3];
    } else {
      x = value.x;
      y = value.y;
      width = own(value, "width") ? value.width : value.w;
      height = own(value, "height") ? value.height : value.h;
    }
    x = system.clampInt(x, 0, surface.width);
    y = system.clampInt(y, 0, surface.height);
    width = system.clampInt(width, 0, surface.width - x);
    height = system.clampInt(height, 0, surface.height - y);
    if (width <= 0 || height <= 0) {
      return null;
    }
    return {
      x: x,
      y: y,
      width: width,
      height: height,
      area: width * height
    };
  }

  function fullRegion(surface) {
    return {
      x: 0,
      y: 0,
      width: surface.width,
      height: surface.height,
      area: surface.width * surface.height
    };
  }

  function regionList(surface, regions) {
    var result = [];
    var value;
    var rect;
    var i;

    if (regions === undefined || regions === null) {
      value = surface.getDirty();
      if (value) {
        rect = normalizeRect(surface, value);
        if (rect) {
          result.push(rect);
        }
      }
      return result;
    }
    if (typeof regions.length === "number" &&
        regions.length >= 4 && typeof regions[0] === "number") {
      rect = normalizeRect(surface, regions);
      if (rect) {
        result.push(rect);
      }
      return result;
    }
    if (typeof regions.length === "number") {
      for (i = 0; i < regions.length; i += 1) {
        rect = normalizeRect(surface, regions[i]);
        if (rect) {
          result.push(rect);
        }
      }
      return result;
    }
    rect = normalizeRect(surface, regions);
    if (rect) {
      result.push(rect);
    }
    return result;
  }

  function boundsFor(list) {
    var x0 = list[0].x;
    var y0 = list[0].y;
    var x1 = list[0].x + list[0].width;
    var y1 = list[0].y + list[0].height;
    var totalArea = 0;
    var rect;
    var i;

    for (i = 0; i < list.length; i += 1) {
      rect = list[i];
      totalArea += rect.area;
      if (rect.x < x0) {
        x0 = rect.x;
      }
      if (rect.y < y0) {
        y0 = rect.y;
      }
      if (rect.x + rect.width > x1) {
        x1 = rect.x + rect.width;
      }
      if (rect.y + rect.height > y1) {
        y1 = rect.y + rect.height;
      }
    }
    return {
      x: x0,
      y: y0,
      width: x1 - x0,
      height: y1 - y0,
      area: (x1 - x0) * (y1 - y0),
      totalArea: totalArea
    };
  }

  function normalizeRegions(surface, driver, regions, options) {
    var list = regionList(surface, regions);
    var capabilities = driver.capabilities || {};
    var settings = options || {};
    var bounds;
    var screenArea = surface.width * surface.height;
    var mergeCoverage = own(settings, "mergeCoverage") ? +settings.mergeCoverage : 0.72;
    var mergeAreaRatio = own(settings, "mergeAreaRatio") ? +settings.mergeAreaRatio : 1.22;
    var mergePixelBudget = own(settings, "mergePixelBudget")
      ? settings.mergePixelBudget | 0
      : surface.width * 24;

    if (list.length === 0) {
      return list;
    }
    if (!capabilities.partialPresent) {
      return [fullRegion(surface)];
    }
    if (list.length === 1) {
      return list;
    }
    bounds = boundsFor(list);
    if (!capabilities.multiRegion || settings.merge !== false &&
        (bounds.totalArea >= screenArea * mergeCoverage ||
         bounds.area <= bounds.totalArea * mergeAreaRatio ||
         bounds.area - bounds.totalArea <= mergePixelBudget * (list.length - 1))) {
      return [bounds];
    }
    return list;
  }

  function regionPixels(regions) {
    var total = 0;
    var i;

    for (i = 0; i < regions.length; i += 1) {
      total += regions[i].width * regions[i].height;
    }
    return total;
  }

  function newStats() {
    return {
      presents: 0,
      regions: 0,
      pixels: 0,
      bytes: 0,
      chunks: 0,
      directTransfers: 0,
      totalUs: 0,
      prepareUs: 0,
      panelUs: 0,
      transferUs: 0
    };
  }

  function copyStats(stats) {
    return {
      presents: stats.presents,
      regions: stats.regions,
      pixels: stats.pixels,
      bytes: stats.bytes,
      chunks: stats.chunks,
      directTransfers: stats.directTransfers,
      totalUs: stats.totalUs,
      prepareUs: stats.prepareUs,
      panelUs: stats.panelUs,
      transferUs: stats.transferUs
    };
  }

  function addStats(target, result, regions) {
    result = result || {};
    target.presents += 1;
    target.regions += own(result, "regions") ? result.regions : regions.length;
    target.pixels += own(result, "pixels") ? result.pixels : regionPixels(regions);
    target.bytes += result.bytes || 0;
    target.chunks += result.chunks || 0;
    target.directTransfers += result.directTransfers || (result.direct ? 1 : 0);
    target.totalUs += result.totalUs || 0;
    target.prepareUs += result.prepareUs || result.pixelUs || 0;
    target.panelUs += result.panelUs || result.windowUs || 0;
    target.transferUs += result.transferUs || result.dataUs || 0;
  }

  system.fullRegion = fullRegion;
  system.normalizeRegions = normalizeRegions;
  system.regionPixels = regionPixels;
  system.newDisplayStats = newStats;
  system.copyDisplayStats = copyStats;
  system.addDisplayStats = addStats;
  system.presentLoaded = true;
})(globalThis);
