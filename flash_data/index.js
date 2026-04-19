var WIFI_SSID = "be65_IoT";
var WIFI_PASSWORD = "1145141919810";

function connectStartupWifi() {
  var status = wifi.status();

  if (status.connected && status.ssid === WIFI_SSID) {
    print("[startup] wifi already connected:", status.ssid, status.ip);
    return status;
  }

  print("[startup] connecting wifi:", WIFI_SSID);
  var nextStatus = wifi.connect(WIFI_SSID, WIFI_PASSWORD);
  print("[startup] wifi connected:", nextStatus.ssid, nextStatus.ip);
  return nextStatus;
}

try {
  globalThis.startupWifiStatus = connectStartupWifi();
} catch (error) {
  print("[startup] wifi connect failed:", error);
}
