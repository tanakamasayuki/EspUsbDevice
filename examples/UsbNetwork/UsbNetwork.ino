#include "EspUsbDevice.h"
#include <WebServer.h>

// USB network device (CDC-NCM) with a built-in DHCP server and web page.
// Plug the board into a PC and it appears as a USB network adapter: the PC is
// handed an address on 192.168.7.0/24 and can browse to http://192.168.7.1/.
// No Wi-Fi, no drivers to install (Windows / macOS / Linux support NCM natively).
//
// This is the "USB config portal" pattern: a device the PC reaches directly over
// USB, without any network setup. See also examples/CompositeHidCdcMsc for
// combining USB functions.

EspUsbDevice device;
EspUsbDeviceNet net(device);
WebServer server(80);

static uint32_t pageViews = 0;

static String buildPage()
{
  const uint32_t up = millis() / 1000;
  String html = F("<!doctype html><html><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>EspUsbDevice NCM</title>"
                  "<style>body{font-family:system-ui,sans-serif;margin:2rem;max-width:34rem}"
                  "h1{font-size:1.3rem}table{border-collapse:collapse}"
                  "td{padding:.25rem .75rem;border-bottom:1px solid #ddd}</style></head><body>");
  html += F("<h1>EspUsbDevice &mdash; USB Network Device</h1>");
  html += F("<p>You reached this page over a USB CDC-NCM link. No Wi-Fi involved.</p><table>");
  html += "<tr><td>Device IP</td><td>" + net.localIP().toString() + "</td></tr>";
  const uint8_t *mac = net.macAddress();
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  html += "<tr><td>Device MAC</td><td>" + String(macStr) + "</td></tr>";
  html += "<tr><td>Uptime</td><td>" + String(up) + " s</td></tr>";
  html += "<tr><td>Page views</td><td>" + String(++pageViews) + "</td></tr>";
  html += F("</table></body></html>");
  return html;
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  // Device is the gateway (192.168.7.1) and runs a DHCP server so the PC gets an
  // address automatically. To instead join a PC-bridged LAN, use net.dhcpClient(true);
  // for a fixed address with no DHCP, just call net.ipConfig(...).
  net.ipConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1), IPAddress(255, 255, 255, 0));
  net.dhcpServer(true);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4032;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice NCM";
  config.serialNumber = "espusb-ncm-web";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  if (!net.beginNetwork())
  {
    Serial.println("NET_BEGIN_FAILED");
    return;
  }

  server.on("/", []()
            { server.send(200, "text/html", buildPage()); });
  server.begin();

  Serial.printf("USB network device ready: http://%s/\n", net.localIP().toString().c_str());
}

void loop()
{
  server.handleClient();
  delay(1);
}
