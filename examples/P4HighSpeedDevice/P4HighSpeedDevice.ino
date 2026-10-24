// en: Start the P4 HighSpeed controller as a USB keyboard.
// ja: P4のHighSpeed controllerでUSBキーボードを開始する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "P4HighSpeedDevice requires ESP32-P4"
#endif

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4040;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice P4 High Speed";
  config.serialNumber = "espusb-p4-hs";

  config.controller = EspUsbController::HighSpeed;

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("P4 high-speed USB keyboard ready");
}

void loop()
{
  static bool sent = false;
  if (!sent && device.ready())
  {
    sent = keyboard.write("Hello from the ESP32-P4 HS device\n");
  }
  delay(10);
}
