#include "EspUsbDevice.h"

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "P4FullSpeedDevice requires ESP32-P4"
#endif

#include "hal/usb_wrap_ll.h"
#include "soc/usb_wrap_struct.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

void setup()
{
  Serial.begin(115200);
  delay(1500);

  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  // ESP32-P4 FullSpeed Device:
  //   TinyUSB rhport 0
  //   internal FS PHY
  //   default USB OTG FS pins: GPIO26=D-, GPIO27=D+
  //
  // Optional FS PHY swap — leave this commented for GPIO26/GPIO27.
  // Uncomment it BEFORE device.begin() to route USB OTG FS to
  // GPIO24=D-, GPIO25=D+ instead. USB Serial/JTAG then moves to
  // GPIO26/GPIO27, so a Serial monitor on GPIO24/GPIO25 disconnects.
  // This is a runtime route change; it does not modify eFuse.
  //
  // usb_wrap_ll_phy_select(&USB_WRAP, 0);

  // The PHY selection only routes D-/D+. VBUS switching, VBUS sensing, and
  // USB-C CC resistors/controllers remain board-specific. Check the schematic
  // before connecting a cable or supplying VBUS.

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4041;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice P4 Full Speed";
  config.serialNumber = "espusb-p4-fs";
  config.controller = EspUsbController::FullSpeed;

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
