var WIFI_SSID = "be65_IoT";
var WIFI_PASSWORD = "1145141919810";

function connectStartupWifi() {
  var status = wifi.status();
  var attempt;
  var lastError = null;

  if (status.connected && status.ssid === WIFI_SSID) {
    print("[startup] wifi already connected:", status.ssid, status.ip);
    return status;
  }

  for (attempt = 1; attempt <= 3; attempt += 1) {
    try {
      print("[startup] connecting wifi:", WIFI_SSID, "attempt", attempt);
      var nextStatus = wifi.connect(WIFI_SSID, WIFI_PASSWORD);
      print("[startup] wifi connected:", nextStatus.ssid, nextStatus.ip);
      return nextStatus;
    } catch (error) {
      lastError = error;
      print("[startup] wifi connect attempt failed:", error);
      if (attempt < 3) {
        sleep(1000);
      }
    }
  }

  throw lastError;
}

try {
  globalThis.startupWifiStatus = connectStartupWifi();
} catch (error) {
  print("[startup] wifi connect failed:", error);
}
