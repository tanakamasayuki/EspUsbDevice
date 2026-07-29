#include "EspUsbDevice.h"

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "P4HighSpeedDevice requires ESP32-P4"
#endif

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

void setup()
{
  Serial.begin(115200);
  delay(1500);

  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4040;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice P4 High Speed";
  config.serialNumber = "espusb-p4-hs";

  // ESP32-P4 HighSpeed Device:
  //   TinyUSB rhport 1
  //   external UTMI high-speed PHY
  //   the board connector wired to that UTMI PHY
  //
  // Connector names differ between P4 boards. Check the board schematic;
  // this is not the USB Serial/JTAG connector and not the GPIO26/GPIO27 FS
  // pair. VBUS/CC handling is also board-specific.
  config.controller = EspUsbController::HighSpeed;

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
