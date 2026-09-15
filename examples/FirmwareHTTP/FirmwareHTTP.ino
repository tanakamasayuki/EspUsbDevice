#include "EspUsbDevice.h"
#include <WebServer.h>
#include <HTTPUpdateServer.h>

// Firmware update over USB with nothing on the host but a browser.
//
// The board is a USB network adapter (CDC-NCM) with its own DHCP server, so the
// PC gets an address the moment it is plugged in and can open a page on the
// device. HTTPUpdateServer adds the stock upload form at /update, and the
// bytes go straight into the spare OTA partition.
//
// No driver to install (Windows, macOS and Linux all speak NCM), no host tool,
// no boot mode. Open http://192.168.7.1/ and pick a file.
//
// Needs a partition scheme with two application partitions - the Arduino
// "Default" one has them, "Huge APP" does not.

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
  Serial.begin(115200);
  delay(1500);

  if (!EspUsbDeviceFirmwareUpdate::available())
  {
    Serial.println("NO_OTA_PARTITION - select a partition scheme with two app partitions");
  }

  // The device hands the PC an address and is the only thing on the link. It
  // does not advertise itself as a gateway, so the PC's own internet route is
  // untouched - see examples/UsbNetwork for the rest of the options.
  net.dhcpServer(true);

  if (!device.begin())
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  if (!net.beginNetwork())
  {
    Serial.println("NETWORK_FAILED");
    return;
  }

  server.on("/", []()
            { server.send(200, "text/html", buildPage()); });
  // Serves a file-upload form at /update and writes what it receives through
  // Arduino's Update library into the same OTA partition
  // EspUsbDeviceFirmwareUpdate would use. Pass a user and password to
  // setup() on anything that leaves a desk.
  updater.setup(&server, "/update");
  server.begin();

  Serial.printf("Ready at http://%s/update\n", net.localIP().toString().c_str());
}

void loop()
{
  server.handleClient();

  // Confirm this image once the USB link is up, cancelling any pending
  // bootloader rollback. A no-op unless the bootloader was built with
  // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE.
  static bool confirmed = false;
  if (!confirmed && net.networkUp())
  {
    confirmed = true;
    EspUsbDeviceFirmwareUpdate::markValid();
  }
  delay(1);
}
