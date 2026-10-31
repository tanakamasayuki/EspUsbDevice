#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

// Ten simultaneous keys: more than the six a boot report can carry.
const uint8_t chord[] = {
    ESP_USB_HID_KEY_A, ESP_USB_HID_KEY_S, ESP_USB_HID_KEY_D, ESP_USB_HID_KEY_F,
    ESP_USB_HID_KEY_G, ESP_USB_HID_KEY_H, ESP_USB_HID_KEY_J, ESP_USB_HID_KEY_K,
    ESP_USB_HID_KEY_L, ESP_USB_HID_KEY_SEMICOLON,
};

void setup() {
  Serial.begin(115200);
  // NKRO changes the report descriptor, so it is chosen before begin().
  esp_err_t error = keyboard.enableNkro();
  if (error != ESP_OK) { Serial.printf("NKRO: %s\n", esp_err_to_name(error)); return; }
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);
  // A BIOS/UEFI selects boot protocol; the held keys are then folded into the
  // 6-key boot report automatically. Runs on the USB worker.
  keyboard.onProtocol([](const EspUsbDeviceHidProtocolEvent &event) {
    Serial.printf("PROTOCOL %s\n", event.protocol == 0 ? "boot" : "report");
  });

  EspUsbDeviceConfig config;
  config.pid = 0x4033;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice NKRO Keyboard";
  config.serialNumber = "espusb-keyboard-nkro";
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("USB NKRO keyboard ready (nkro=%u)\n", keyboard.nkroEnabled() ? 1 : 0);
  Serial.println("Focus a key tester on the Host, then send c on this port");
}

void loop() {
  if (!device.ready() || !Serial.available() || Serial.read() != 'c') { delay(1); return; }

  // The whole held-key state travels as ONE report, so the chord appears at
  // once. pressUsage() per key would send ten reports, one key at a time.
  EspUsbDeviceNkroKeyboardReport report;
  for (uint8_t usage : chord) report.press(usage);
  // Modifier usages 0xE0-0xE7 go into report.modifiers; 0xE1 is Left Shift.
  report.press(0xe1);
  if (!keyboard.sendReport(report)) {
    Serial.printf("SEND_FAILED %s\n", device.lastErrorName());
    return;
  }
  delay(200);
  keyboard.releaseAll();
  // heldState() is what the Host was last told; diff against it if needed.
  Serial.printf("after releaseAll: A still down? %s\n",
      keyboard.heldState().isDown(ESP_USB_HID_KEY_A) ? "yes" : "no");
  Serial.printf("sent %u-key chord (protocol=%s)\n", unsigned(sizeof(chord)),
      keyboard.protocol() == 0 ? "boot" : "report");
}
