// en: Upload an application through a web page over USB networking.
// ja: USBネットワーク上のWebページからapplicationを更新する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"
#include <WebServer.h>
#include <HTTPUpdateServer.h>


// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceNet net(device);
WebServer server(80);
HTTPUpdateServer updater;

static String buildPage()
{
  String html = F("<!doctype html><html><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>EspUsbDevice firmware</title>"
                  "<style>body{font-family:system-ui,sans-serif;margin:2rem;max-width:34rem}"
                  "h1{font-size:1.3rem}td{padding:.25rem .75rem;border-bottom:1px solid #ddd}"
                  "</style></head><body>");
  html += F("<h1>EspUsbDevice &mdash; firmware update over USB</h1>");
  html += F("<p>This page arrived over a USB CDC-NCM link. No Wi-Fi involved.</p><table>");
  html += "<tr><td>Running from</td><td>" + String(ESP.getSketchMD5().substring(0, 8)) + "</td></tr>";
  html += "<tr><td>Update target</td><td>" +
          String(EspUsbDeviceFirmwareUpdate::targetLabel()
                     ? EspUsbDeviceFirmwareUpdate::targetLabel()
                     : "none") +
          "</td></tr>";
  html += "<tr><td>Room for</td><td>" +
          String(static_cast<unsigned>(EspUsbDeviceFirmwareUpdate::capacity())) +
          " bytes</td></tr>";
  html += "<tr><td>Uptime</td><td>" + String(millis() / 1000) + " s</td></tr>";
  html += F("</table><p><a href='/update'>Upload new firmware</a></p></body></html>");
  return html;
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  if (!EspUsbDeviceFirmwareUpdate::available())
  {
    Serial.println("NO_OTA_PARTITION - select a partition scheme with two app partitions");
  }

  // en: The device hands the PC an address and is the only thing on the link. It does not advertise itself as a gateway, so the PC's own internet route is untouched - see examples/UsbNetwork for the rest of the options.
  // ja: PCへIPを配る直接接続の構成。主回線のdefault routeを置き換える設定とは分ける。
  net.dhcpServer(true);

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin())
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  // en: USB enumeration comes first; IP configuration and the application connection are later steps.
  // ja: USB列挙の次にIP設定を行い、その後にアプリの接続を確認する。
  if (!net.beginNetwork())
  {
    Serial.println("NETWORK_FAILED");
    return;
  }

  server.on("/", []()
            { server.send(200, "text/html", buildPage()); });
  // en: Serves a file-upload form at /update and writes what it receives through Arduino's Update library into the same OTA partition EspUsbDeviceFirmwareUpdate would use. Pass a user and password to setup() on anything that leaves a desk.
  // ja: Webのuploadを更新APIへ渡す。フォーム受付とimage検証の完了は別に確認する。
  updater.setup(&server, "/update");
  server.begin();

  Serial.printf("Ready at http://%s/update\n", net.localIP().toString().c_str());
}

void loop()
{
  server.handleClient();

  // en: Confirm this image once the USB link is up, cancelling any pending bootloader rollback. A no-op unless the bootloader was built with CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE.
  // ja: このアプリが動作したことを確認して検証状態を更新する。署名・認証の代わりではない。
  static bool confirmed = false;
  if (!confirmed && net.networkUp())
  {
    confirmed = true;
    EspUsbDeviceFirmwareUpdate::markValid();
  }
  delay(1);
}
