#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceHidMouse mouse(device);

void setup() {
  Serial.begin(115200);
  EspUsbDeviceConfig config;
  config.pid = 0x4002;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Mouse";
  config.serialNumber = "espusb-mouse";
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("USB mouse ready");
}

void loop() {
  static uint8_t step = 0;
  static uint32_t last = 0;
  const uint32_t now = millis();
  // Nothing is sent until the Host has configured the device.
  if (!device.ready() || uint32_t(now - last) < 1000) { delay(1); return; }
  last = now;

  // Movement is a relative delta, not a screen position.
  bool ok = false;
  switch (step++ % 6) {
    case 0: ok = mouse.move(30, 0); break;
    case 1: ok = mouse.move(0, 30); break;
    case 2: ok = mouse.move(-30, 0); break;
    case 3: ok = mouse.move(0, -30); break;
    case 4: ok = mouse.wheel(1); break;
    default:
      // press() keeps the button held until the matching release().
      ok = mouse.press(ESP_USB_DEVICE_MOUSE_LEFT);
      delay(50);
      ok = mouse.release(ESP_USB_DEVICE_MOUSE_LEFT) && ok;
      break;
  }
  if (!ok) Serial.printf("MOUSE_FAILED %s buttons=0x%02x\n", device.lastErrorName(), mouse.buttons());
}
