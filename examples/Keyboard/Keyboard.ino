#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

// Text, a second layout, a held modifier and a raw usage. Sent only on request.
void demo() {
  if (!keyboard.write("Hi from EspUsbDevice\n")) {
    Serial.printf("WRITE_FAILED %s\n", device.lastErrorName());
    return;
  }
  // The layout maps characters to key positions; the Host's own layout setting
  // decides which character appears. These symbols move on a JIS keyboard.
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP);
  keyboard.write("@[]:\"");
  keyboard.tapKey('\n');
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);
  // Hold Shift+B until the matching release; usages are key positions.
  keyboard.pressUsage(ESP_USB_HID_KEY_B, ESP_USB_DEVICE_MOD_LEFT_SHIFT);
  keyboard.releaseUsage(ESP_USB_HID_KEY_B);
  keyboard.releaseAll();
  // Keys outside the ASCII mapping are sent by HID usage ID.
  keyboard.tapUsage(ESP_USB_HID_KEY_LANG1);
  // ledState() is updated with or without a callback.
  const EspUsbDeviceHidKeyboardOutputReport leds = keyboard.ledState();
  Serial.printf("last_leds=0x%02x caps=%u\n", leds.leds, leds.capsLock ? 1 : 0);
}

void setup() {
  Serial.begin(115200);
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);
  // Runs on the USB worker when the Host changes the Lock LEDs. Register before
  // begin(); afterwards it returns ESP_ERR_INVALID_STATE.
  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report) {
    Serial.printf("LEDS num=%u caps=%u scroll=%u raw=0x%02x\n",
        report.numLock ? 1 : 0, report.capsLock ? 1 : 0,
        report.scrollLock ? 1 : 0, report.leds);
  });

  EspUsbDeviceConfig config;
  config.pid = 0x4001;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Keyboard";
  config.serialNumber = "espusb-keyboard";
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("USB keyboard ready: type here to send keys, ~ runs the demo");
}

void loop() {
  // Type into the separate debug serial port to send keys over the data USB.
  // Select a text editor on the USB Host before entering text.
  if (device.ready() && Serial.available()) {
    const char key = char(Serial.read());
    if (key == '~') demo();
    else if (key != '\r') keyboard.tapKey(key);  // Unmapped characters are skipped.
  }
  delay(1);
}
