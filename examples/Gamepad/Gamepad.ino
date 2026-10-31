#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceHidGamepad gamepad(device);

// CENTER (0) means "no direction"; 1-8 run clockwise from UP.
const uint8_t hats[] = {
    ESP_USB_DEVICE_GAMEPAD_HAT_CENTER,
    ESP_USB_DEVICE_GAMEPAD_HAT_UP, ESP_USB_DEVICE_GAMEPAD_HAT_UP_RIGHT,
    ESP_USB_DEVICE_GAMEPAD_HAT_RIGHT, ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_RIGHT,
    ESP_USB_DEVICE_GAMEPAD_HAT_DOWN, ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_LEFT,
    ESP_USB_DEVICE_GAMEPAD_HAT_LEFT, ESP_USB_DEVICE_GAMEPAD_HAT_UP_LEFT,
};
const int8_t axis[] = {0, 64, 127, 64, 0, -64, -127, -64};
uint8_t step = 0;

void sendStep() {
  const uint8_t i = step % 8;
  const int8_t x = axis[i], y = axis[(i + 2) % 8], z = axis[(i + 4) % 8], rz = axis[(i + 6) % 8];
  const uint8_t hat = hats[step % sizeof(hats)];
  const uint32_t buttons = 1UL << (step % 8);  // A bitmap of held buttons.
  // Each report carries the complete state: every axis, the hat and all buttons.
  if (gamepad.send(x, y, z, rz, -x, -y, hat, buttons)) {
    Serial.printf("GAMEPAD_TX x=%d y=%d z=%d rz=%d hat=%u buttons=0x%08lx\n",
        x, y, z, rz, hat, (unsigned long)buttons);
  } else {
    Serial.printf("GAMEPAD_SEND_FAILED %s\n", device.lastErrorName());
  }
  step++;
}

void setup() {
  Serial.begin(115200);
  EspUsbDeviceConfig config;
  config.pid = 0x4008;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Gamepad";
  config.serialNumber = "espusb-gamepad";
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("USB gamepad ready");
}

void loop() {
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (device.ready() && uint32_t(now - last) >= 500) {
    last = now;
    sendStep();
  }
  delay(1);
}
