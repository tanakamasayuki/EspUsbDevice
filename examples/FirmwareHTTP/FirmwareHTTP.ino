// Firmware update from a browser over a USB network link (CDC-NCM). The board
// hands the PC an address and serves Arduino's upload form at /update.
// Partition requirements and host-side steps: README.md / README.ja.md.
#include <EspUsbDevice.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>

EspUsbDevice device;
EspUsbDeviceNet net(device);
WebServer server(80);
HTTPUpdateServer updater;

String buildPage() {
  const char *target = EspUsbDeviceFirmwareUpdate::targetLabel();
  String html = F("<!doctype html><html><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>EspUsbDevice firmware</title>"
                  "<style>body{font-family:system-ui,sans-serif;margin:2rem;max-width:34rem}"
                  "h1{font-size:1.3rem}td{padding:.25rem .75rem;border-bottom:1px solid #ddd}"
                  "</style></head><body>");
  html += F("<h1>EspUsbDevice &mdash; firmware update over USB</h1>");
  html += F("<p>This page arrived over a USB CDC-NCM link. No Wi-Fi involved.</p><table>");
  html += "<tr><td>Running from</td><td>" + ESP.getSketchMD5().substring(0, 8) + "</td></tr>";
  html += "<tr><td>Update target</td><td>" + String(target ? target : "none") + "</td></tr>";
  html += "<tr><td>Room for</td><td>" +
          String(unsigned(EspUsbDeviceFirmwareUpdate::capacity())) + " bytes</td></tr>";
  html += "<tr><td>Uptime</td><td>" + String(millis() / 1000) + " s</td></tr>";
  html += F("</table><p><a href='/update'>Upload new firmware</a></p></body></html>");
  return html;
}

void setup() {
  Serial.begin(115200); // Separate debug connection.
  delay(1500);

  if (!EspUsbDeviceFirmwareUpdate::available()) {
    Serial.println("NO_OTA_PARTITION - select a partition scheme with two app partitions");
  }

  EspUsbNetConfig netConfig; // Default NTB sizes; IP needs maxFrameBytes >= 1514.
  netConfig.name = "Firmware update";
  esp_err_t error = net.configure(netConfig);
  if (error != ESP_OK) { Serial.printf("NET_CONFIGURE_FAILED %s\n", esp_err_to_name(error)); return; }
  // The device is the only thing on the link and hands the PC an address on
  // 192.168.7.0/24 (the ipConfig() default). It does not advertise itself as a
  // gateway or DNS, so the PC's own internet route is untouched.
  net.dhcpServer(true);

  if (!device.begin()) {
    Serial.printf("USB_BEGIN_FAILED %s net=%s\n", device.lastErrorName(),
                  esp_err_to_name(net.startError()));
    return;
  }
  // USB enumeration first; the IP layer is a separate, later step.
  if (!net.beginNetwork()) { Serial.println("NETWORK_FAILED"); return; }

  server.on("/", []() { server.send(200, "text/html", buildPage()); });
  // Upload form at /update. It writes through Arduino's Update library into
  // the same OTA partition EspUsbDeviceFirmwareUpdate would use. Pass a user
  // and password - setup(&server, "/update", user, pass) - before this leaves a desk.
  updater.setup(&server, "/update");
  server.begin();
  Serial.printf("Ready at http://%s/update\n", net.localIP().toString().c_str());
}

void loop() {
  server.handleClient();
  // Confirm this image once the USB network is up, cancelling a pending
  // bootloader rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE only).
  static bool confirmed = false;
  if (!confirmed && net.networkUp()) {
    confirmed = true;
    EspUsbDeviceFirmwareUpdate::markValid();
  }
  delay(1);
}
