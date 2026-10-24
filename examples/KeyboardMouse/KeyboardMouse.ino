// en: Combine keyboard and mouse reports on one HID device.
// ja: 一つのHID機器でキーボードとマウスのreportを組み合わせる。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4003;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Keyboard Mouse";
  config.serialNumber = "espusb-keyboard-mouse";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB keyboard + mouse ready");
}

void loop()
{
  static uint32_t lastSendMs = 0;
  const uint32_t now = millis();
  if (now - lastSendMs < 3000)
  {
    delay(1);
    return;
  }
  lastSendMs = now;

  // en: Send keyboard state or key operations, not serial text; account for the corresponding release.
  // ja: シリアル文字列ではなくキー状態・操作を送る。対応する解放も考える。
  if (!keyboard.write("K"))
  {
    Serial.printf("KEY_FAILED error=%s\n", device.lastErrorName());
  }
  // en: Mouse coordinates are relative deltas; buttons remain state until explicitly released.
  // ja: マウスの座標は相対差分。ボタンの保持状態は明示的な解放まで続く。
  if (!mouse.move(40, 0))
  {
    Serial.printf("MOVE_FAILED error=%s\n", device.lastErrorName());
  }
  if (!mouse.click(ESP_USB_DEVICE_MOUSE_LEFT))
  {
    Serial.printf("CLICK_FAILED error=%s\n", device.lastErrorName());
  }
}
