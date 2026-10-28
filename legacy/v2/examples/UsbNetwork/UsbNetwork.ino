// en: Serve a local web page through a USB network interface.
// ja: USBネットワークinterface経由でローカルWebページを提供する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"
#include <WebServer.h>


// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
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
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  // en: Device is the gateway (192.168.7.1) and runs a DHCP server so the PC gets an address automatically. To instead join a PC-bridged LAN, use net.dhcpClient(true); for a fixed address with no DHCP, just call net.ipConfig(...).
  // ja: 直接接続ではdevice側にIPとDHCP serverを設定する。別LANへ参加する構成とは分ける。
  net.ipConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1), IPAddress(255, 255, 255, 0));
  net.dhcpServer(true);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4032;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice NCM";
  config.serialNumber = "espusb-ncm-web";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  // en: USB enumeration comes first; IP configuration and the application connection are later steps.
  // ja: USB列挙の次にIP設定を行い、その後にアプリの接続を確認する。
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
