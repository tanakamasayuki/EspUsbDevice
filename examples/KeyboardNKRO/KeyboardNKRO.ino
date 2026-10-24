// en: Send a held-key bitmap and its explicit release.
// ja: 保持キーのbitmapと明示的な解放を送る。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

// en: N-key rollover keyboard: any number of keys can be held at once, unlike the 6-key boot report. Enable it with keyboard.enableNkro() before device.begin(). The device still answers boot protocol (BIOS) with the 6-key format, and the bitmap covers usages 0x00-0xDF so International/LANG (JIS) keys work too.
// ja: begin()前にNKROを有効にする。boot protocolでは対応する少数キー形式を使う。

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

// Ten simultaneous keys — impossible with the 6KRO boot report, easy with NKRO.
static const uint8_t chord[] = {
    ESP_USB_HID_KEY_A, ESP_USB_HID_KEY_S, ESP_USB_HID_KEY_D, ESP_USB_HID_KEY_F,
    ESP_USB_HID_KEY_G, ESP_USB_HID_KEY_H, ESP_USB_HID_KEY_J, ESP_USB_HID_KEY_K,
    ESP_USB_HID_KEY_L, ESP_USB_HID_KEY_SEMICOLON,
};

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  keyboard.enableNkro();
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4033;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice NKRO Keyboard";
  config.serialNumber = "espusb-keyboard-nkro";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.printf("USB NKRO keyboard ready (nkro=%u)\n", keyboard.nkroEnabled() ? 1 : 0);
}

void loop()
{
  static uint32_t lastSendMs = 0;
  const uint32_t now = millis();
  if (now - lastSendMs < 4000)
  {
    delay(1);
    return;
  }
  lastSendMs = now;

  if (!device.ready())
  {
    return;
  }

  // en: Hold all ten keys down at the same time, then release them together. The whole held-key state travels as ONE report, so the host sees the chord appear at once. Calling pressUsage() ten times would work too, but it sends ten reports and the host would observe the keys arriving one by one.
  // ja: 全保持状態を一つのreportへまとめる。キーごとに別送するとホストは順次到着として観測する。
  EspUsbDeviceNkroKeyboardReport report;
  for (size_t i = 0; i < sizeof(chord); i++)
  {
    report.press(chord[i]);
  }
  // en: Modifier usages (0xE0-0xE7) are routed into report.modifiers, not the bitmap, so press() takes them like any other key. 0xE1 is Left Shift.
  // ja: Shift等の修飾usageはbitmapではなくmodifiersへ入る。通常キーと同じpress()で指定できる。
  report.press(0xe1);
  // en: Send keyboard state or key operations, not serial text; account for the corresponding release.
  // ja: シリアル文字列ではなくキー状態・操作を送る。対応する解放も考える。
  keyboard.sendReport(report);
  delay(200);
  keyboard.releaseAll();

  // en: heldState() is what the host was last told, so a state-driven sketch can diff against it instead of keeping its own copy.
  // ja: heldState()は最後にホストへ知らせた状態。次の状態との差を取るときに使える。
  Serial.printf("after releaseAll: A still down? %s\n",
                keyboard.heldState().isDown(ESP_USB_HID_KEY_A) ? "yes" : "no");

  Serial.printf("sent %u-key chord (protocol=%s)\n",
                (unsigned)sizeof(chord),
                keyboard.protocol() == 0 ? "boot" : "report");
}
