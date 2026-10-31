#include <EspUsbDevice.h>

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "P4FullSpeedDevice requires ESP32-P4"
#endif

// Optional D-/D+ routing, see setup(). An ESP-IDF HAL call, not EspUsbDevice API.
#include "hal/usb_wrap_ll.h"
#include "soc/usb_wrap_struct.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
bool greeted = false;

void setup() {
  Serial.begin(115200); // Debug connection: find its route on your board.
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  // The FS controller uses GPIO26 (D-) / GPIO27 (D+) by default. Uncomment to
  // move it to GPIO24 / GPIO25; USB Serial/JTAG then moves to GPIO26 / GPIO27
  // and a debug link on GPIO24 / GPIO25 disappears. This routes only D-/D+.
  // usb_wrap_ll_phy_select(&USB_WRAP, 0);

  EspUsbDeviceConfig config;
  config.pid = 0x4041;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice P4 Full Speed";
  config.serialNumber = "espusb-p4-fs";
  // Auto would pick the HS controller on P4; ask for the FS one explicitly.
  config.controller = EspUsbController::FullSpeed;
  // false forces "VBUS present". Set true only when the board routes a VBUS
  // detect signal to this controller; it adds no GPIO or circuit.
  config.vbusSensing = false;
  if (!device.begin(config)) {
    Serial.printf("USB start failed: %s\n", device.lastErrorName());
    return;
  }
  Serial.println("P4 full-speed USB keyboard ready");
}

void loop() {
  // Select a text editor on the USB Host: one line is typed after enumeration.
  if (!greeted && device.ready()) greeted = keyboard.write("Hello from the ESP32-P4 FS device\n");
  delay(10);
}
