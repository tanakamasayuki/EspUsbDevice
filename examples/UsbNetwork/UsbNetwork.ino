#include <EspUsbDevice.h>
#include <WebServer.h>

EspUsbDevice device;
EspUsbDeviceNet net(device);
WebServer server(80);
uint32_t pageViews = 0;

String buildPage() {
  String html = F("<!doctype html><html><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>EspUsbDevice NCM</title>"
                  "<style>body{font-family:system-ui,sans-serif;margin:2rem;max-width:34rem}"
                  "h1{font-size:1.3rem}table{border-collapse:collapse}"
                  "td{padding:.25rem .75rem;border-bottom:1px solid #ddd}</style></head><body>"
                  "<h1>EspUsbDevice &mdash; USB Network Device</h1>"
                  "<p>You reached this page over a USB CDC-NCM link. No Wi-Fi involved.</p><table>");
  const uint8_t *mac = net.macAddress();
  char macText[18];
  snprintf(macText, sizeof(macText), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  const EspUsbNetStats stats = net.stats();
  html += "<tr><td>Device IP</td><td>" + net.localIP().toString() + "</td></tr>";
  html += "<tr><td>Device MAC</td><td>" + String(macText) + "</td></tr>";
  html += "<tr><td>Uptime</td><td>" + String(millis() / 1000) + " s</td></tr>";
  html += "<tr><td>Page views</td><td>" + String(++pageViews) + "</td></tr>";
  html += "<tr><td>USB frames sent / received</td><td>" + String(stats.txFrames) + " / " + String(stats.rxFrames) + "</td></tr>";
  html += "<tr><td>Rejected NTBs</td><td>" + String(stats.invalidNtbs) + "</td></tr>";
  html += F("</table></body></html>");
  return html;
}

void setup() {
  Serial.begin(115200); // Separate debug connection.
  EspUsbNetConfig config; // Defaults: 3200-byte NTBs, 3 transmit and 2 receive buffers.
  config.name = "EspUsbDevice NCM";
  esp_err_t error = net.configure(config);
  // The device takes 192.168.7.1 and hands the PC an address by DHCP. It does
  // not advertise itself as a gateway or DNS, so the PC keeps its own default
  // route. Use net.dhcpClient(true) to join a PC-bridged LAN instead, or
  // neither for a fixed address.
  if (error == ESP_OK)
    error = net.ipConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1), IPAddress(255, 255, 255, 0));
  if (error == ESP_OK) error = net.dhcpServer(true);
  if (error != ESP_OK) {
    Serial.printf("Network setup failed: %s\n", esp_err_to_name(error));
    return;
  }

  EspUsbDeviceConfig identity;
  identity.pid = 0x4032;
  identity.manufacturer = "EspUsb";
  identity.product = "EspUsbDevice NCM";
  identity.serialNumber = "espusb-ncm-web";
  if (!device.begin(identity)) {
    Serial.printf("USB start failed: %s\n", device.lastErrorName());
    return;
  }
  // The IP stack (esp_netif and lwIP) starts after USB, from the sketch task.
  if (!net.beginNetwork()) {
    Serial.println("Network start failed");
    return;
  }
  server.on("/", []() { server.send(200, "text/html", buildPage()); });
  server.begin();
  Serial.printf("USB network device ready: http://%s/\n", net.localIP().toString().c_str());
}

void loop() {
  server.handleClient();
  static bool wasUp = false;
  const bool up = net.linkUp();
  if (up != wasUp) {
    wasUp = up;
    Serial.printf("USB link %s\n", up ? "up" : "down");
  }
  delay(1);
}
