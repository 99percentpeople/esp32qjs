// var WIFI_SSID = "Chevalier-gen";
// var WIFI_PASSWORD = "F@wq2zpv";
var WIFI_SSID = "be65_IoT";
var WIFI_PASSWORD = "1145141919810";
var WIFI_CONNECT_TIMEOUT_MS = 15000;

load("_sys/display.js");
load("_sys/ui.js");

function connectStartupWifi(attempt) {
  var status = wifi.status();
  var currentAttempt = attempt || 1;

  if (status.connected && status.ssid === WIFI_SSID) {
    globalThis.startupWifiStatus = status;
    print("[startup] wifi already connected:", status.ssid, status.ip);
    return;
  }

  print("[startup] connecting wifi:", WIFI_SSID, "attempt", currentAttempt);
  wifi.connect(WIFI_SSID, WIFI_PASSWORD, WIFI_CONNECT_TIMEOUT_MS, function (nextStatus, error) {
    if (error) {
      globalThis.startupWifiError = error;
      print("[startup] wifi connect attempt failed:", error);
      if (currentAttempt < 3) {
        setTimeout(function () {
          connectStartupWifi(currentAttempt + 1);
        }, 1000);
      }
      return;
    }

    globalThis.startupWifiError = null;
    globalThis.startupWifiStatus = nextStatus;
    print("[startup] wifi connected:", nextStatus.ssid, nextStatus.ip);
  });
}

try {
  globalThis.startupWifiStatus = null;
  globalThis.startupWifiError = null;
  connectStartupWifi(1);
} catch (error) {
  print("[startup] wifi connect failed:", error);
}
