// en: Send relative pointer motion, wheel input and button transitions.
// ja: ポインタの相対移動・wheel・ボタンの変化を送る。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceHidMouse mouse(device);

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4002;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Mouse";
  config.serialNumber = "espusb-mouse";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB mouse ready");
}

void loop()
{
  static uint8_t step = 0;
  static uint32_t lastSendMs = 0;
  const uint32_t now = millis();
  if (now - lastSendMs < 1000)
  {
    delay(1);
    return;
  }
  lastSendMs = now;

  bool ok = false;
  switch (step++ % 6)
  {
  case 0:
    ok = mouse.move(30, 0);
    break;
  case 1:
    ok = mouse.move(0, 30);
    break;
  case 2:
    ok = mouse.move(-30, 0);
    break;
  case 3:
    ok = mouse.move(0, -30);
    break;
  case 4:
    ok = mouse.wheel(1);
    break;
  default:
    ok = mouse.press(ESP_USB_DEVICE_MOUSE_LEFT);
    delay(50);
    ok = ok && mouse.release(ESP_USB_DEVICE_MOUSE_LEFT);
    break;
  }

  if (!ok)
  {
    Serial.printf("MOUSE_FAILED error=%s\n", device.lastErrorName());
  }
}
