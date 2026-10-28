// en: Send keyboard text and inspect host Lock LED reports.
// ja: キーボードの文字を送り、ホストのLock LED通知を読む。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

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

  // The callback fires the moment the host changes the Lock LEDs. It is a single
  // slot, so keyboard.ledState() below is the way to read the same state when
  // something else (an integration layer, say) already took it.
  // en: The USB task delivers this host notification; it is not a callback executed by loop().
  // ja: ホストからの通知はUSBタスクで届く。loop()が実行するcallbackではない。
  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report)
                          {
                            Serial.printf("LEDS num=%u caps=%u scroll=%u raw=0x%02x\n",
                                          report.numLock ? 1 : 0,
                                          report.capsLock ? 1 : 0,
                                          report.scrollLock ? 1 : 0,
                                          report.leds);
                          });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4001;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Keyboard";
  config.serialNumber = "espusb-keyboard";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB keyboard ready");
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
  if (!keyboard.write("Hi from EspUsbDevice\n"))
  {
    Serial.printf("WRITE_FAILED error=%s\n", device.lastErrorName());
  }

  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP);
  keyboard.write("@[]:\"");
  keyboard.tapKey('\n');
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  // Raw HID usage remains available for keys that are not part of the ASCII wrapper.
  keyboard.tapUsage(ESP_USB_HID_KEY_LANG1);
  // en: ledState() holds the latest host output report - no need to mirror it into a global from the callback.
  // ja: 最新のホストLED状態はledState()から読める。callbackで別のglobalへ複製しなくてもよい。
  Serial.printf("last_leds=0x%02x caps=%u\n",
                keyboard.ledState().leds,
                keyboard.ledState().capsLock ? 1 : 0);
}
