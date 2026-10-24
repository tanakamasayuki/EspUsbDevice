// en: Start the P4 FullSpeed controller as a USB keyboard.
// ja: P4のFullSpeed controllerでUSBキーボードを開始する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "P4FullSpeedDevice requires ESP32-P4"
#endif

#include "hal/usb_wrap_ll.h"
#include "soc/usb_wrap_struct.h"

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

  // GPIO24=D-, GPIO25=D+ instead. USB Serial/JTAG then moves to

  // en: The PHY selection only routes D-/D+. VBUS switching, VBUS sensing, and USB-C CC resistors/controllers remain board-specific. Check the schematic before connecting a cable or supplying VBUS.
  // ja: PHYの変更はD−/D+の経路だけ。VBUSやUSB-C CCの処理は基板の回路図で別に確認する。

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4041;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice P4 Full Speed";
  config.serialNumber = "espusb-p4-fs";
  config.controller = EspUsbController::FullSpeed;

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("P4 full-speed USB keyboard ready");
}

void loop()
{
  static bool sent = false;
  if (!sent && device.ready())
  {
    sent = keyboard.write("Hello from the ESP32-P4 FS device\n");
  }
  delay(10);
}
