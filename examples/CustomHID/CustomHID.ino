// en: Describe and send a custom input-only HID report.
// ja: 独自の入力専用HID reportを定義して送る。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

static const uint8_t REPORT_DESCRIPTOR[] = {
    0x06, 0x00, 0xff, // Usage Page (Vendor Defined 0xff00)
    0x09, 0x01,       // Usage (1)
    0xa1, 0x01,       // Collection (Application)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xff, 0x00, //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x08,       //   Report Count (8)
    0x09, 0x01,       //   Usage (1)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0xc0,             // End Collection
};

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceHidCustom customHid(device, REPORT_DESCRIPTOR, sizeof(REPORT_DESCRIPTOR), 8);

static uint32_t lastReportMs = 0;
static uint8_t counter = 0;

static void sendCustomReport()
{
  uint8_t report[8] = {
      0x45,
      0x53,
      0x50,
      counter,
      static_cast<uint8_t>(millis() & 0xff),
      static_cast<uint8_t>((millis() >> 8) & 0xff),
      0xaa,
      0x55,
  };

  if (customHid.sendReport(report, sizeof(report)))
  {
    Serial.printf("CUSTOM_HID_TX counter=%u\n", counter);
    counter++;
  }
  else
  {
    Serial.printf("CUSTOM_HID_FAILED %s\n", device.lastErrorName());
  }
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4004;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Custom HID";
  config.serialNumber = "espusb-custom-hid";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB custom HID ready");
}

void loop()
{
  const uint32_t now = millis();
  if (now - lastReportMs >= 1000)
  {
    lastReportMs = now;
    sendCustomReport();
  }

  delay(1);
}
