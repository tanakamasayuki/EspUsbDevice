#include <EspUsbDevice.h>

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "P4HighSpeedDevice requires ESP32-P4"
#endif

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
bool greeted = false;

void setup() {
  Serial.begin(115200); // Debug connection: find its route on your board.
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  EspUsbDeviceConfig config;
  config.pid = 0x4040;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice P4 High Speed";
  config.serialNumber = "espusb-p4-hs";
  // Auto picks this controller on P4 too; naming it keeps the choice explicit.
  config.controller = EspUsbController::HighSpeed;
  // false forces "VBUS present". Set true only when the board routes a VBUS
  // detect signal to this controller; it adds no GPIO or circuit.
  config.vbusSensing = false;
  if (!device.begin(config)) {
    Serial.printf("USB start failed: %s\n", device.lastErrorName());
    return;
  }
  Serial.println("P4 high-speed USB keyboard ready");
}

void loop() {
  // Select a text editor on the USB Host: one line is typed after enumeration.
  if (!greeted && device.ready()) {
    greeted = keyboard.write("Hello from the ESP32-P4 HS device\n");
    // The speed the host negotiated at bus reset: Full behind a full-speed hub
    // or host, even on this high-speed controller.
    if (greeted) Serial.printf("link speed: %s\n", device.speed() == EspUsbBusSpeed::High ? "high" : "full");
  }
  delay(10);
}
