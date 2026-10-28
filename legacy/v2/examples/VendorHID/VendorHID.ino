// en: Exchange vendor-defined HID Input, Output and Feature reports.
// ja: Vendor定義HIDのInput・Output・Feature reportを扱う。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceHidVendor vendor(device);

static uint32_t lastInputMs = 0;
static uint32_t inputCounter = 0;

static void printReport(const char *prefix, const EspUsbDeviceHidReport &report)
{
  Serial.printf("%s report_id=%u type=%u len=%u data=",
                prefix,
                report.reportId,
                report.reportType,
                report.length);
  for (uint16_t i = 0; i < report.length && report.data; ++i)
  {
    Serial.printf("%02x", report.data[i]);
    if (i + 1 < report.length)
    {
      Serial.print(' ');
    }
  }
  Serial.println();
}

static void sendInputReport()
{
  uint8_t payload[63] = {};
  payload[0] = 'E';
  payload[1] = 'S';
  payload[2] = 'P';
  payload[3] = static_cast<uint8_t>(inputCounter & 0xff);
  payload[4] = static_cast<uint8_t>((inputCounter >> 8) & 0xff);
  payload[5] = static_cast<uint8_t>((millis() / 1000) & 0xff);

  if (vendor.sendInput(payload, sizeof(payload)))
  {
    Serial.printf("VENDOR_INPUT counter=%lu\n", static_cast<unsigned long>(inputCounter));
    inputCounter++;
  }
  else
  {
    Serial.printf("VENDOR_INPUT_FAILED %s\n", device.lastErrorName());
  }
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  // en: The USB task delivers this host notification; it is not a callback executed by loop().
  // ja: ホストからの通知はUSBタスクで届く。loop()が実行するcallbackではない。
  vendor.onOutputReport([](const EspUsbDeviceHidReport &report)
                        { printReport("VENDOR_OUTPUT", report); });

  vendor.onFeatureReport([](const EspUsbDeviceHidReport &report)
                         { printReport("VENDOR_FEATURE", report); });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4005;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Vendor HID";
  config.serialNumber = "espusb-vendor-hid";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB vendor HID ready");
}

void loop()
{
  const uint32_t now = millis();
  if (now - lastInputMs >= 1000)
  {
    lastInputMs = now;
    sendInputReport();
  }

  delay(1);
}
