#include <EspUsbDevice.h>

EspUsbDevice device;
// Both Functions share one HID interface and are told apart by report ID.
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);

void setup() {
  Serial.begin(115200);
  EspUsbDeviceConfig config;
  config.pid = 0x4003;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Keyboard Mouse";
  config.serialNumber = "espusb-keyboard-mouse";
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  // The IDs and the largest report are decided by begin() from the descriptors.
  Serial.printf("USB keyboard + mouse ready (keyboard id=%u mouse id=%u input=%u bytes)\n",
      keyboard.compositeReportId(), mouse.compositeReportId(),
      unsigned(keyboard.inputReportCapacity()));
  Serial.println("Focus a scratch editor on the Host, then send k on this port");
}

void loop() {
  if (!device.ready() || !Serial.available() || Serial.read() != 'k') { delay(1); return; }
  if (!keyboard.write("K")) Serial.printf("KEY_FAILED %s\n", device.lastErrorName());
  // Mouse movement is a relative delta; click() pairs press and release.
  if (!mouse.move(40, 0)) Serial.printf("MOVE_FAILED %s\n", device.lastErrorName());
  if (!mouse.click(ESP_USB_DEVICE_MOUSE_LEFT)) Serial.printf("CLICK_FAILED %s\n", device.lastErrorName());
}
