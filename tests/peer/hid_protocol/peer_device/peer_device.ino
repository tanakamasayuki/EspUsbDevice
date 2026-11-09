#include <Arduino.h>
#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceHidKeyboard *keyboard = nullptr;
EspUsbDeviceHidMouse *mouse = nullptr;
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "keyboard" || command == "mouse") {
    delete keyboard; keyboard = nullptr; delete mouse; mouse = nullptr;
    if (command == "keyboard") { keyboard = new EspUsbDeviceHidKeyboard(device); keyboard->enableNkro(true); }
    else mouse = new EspUsbDeviceHidMouse(device);
    EspUsbDeviceConfig config; config.product = "v3 HID protocol";
    Serial.printf("STARTED %u\r\n", unsigned(device.begin(config)));
  } else if (command.startsWith("send ")) {
    const uint8_t value = command.substring(5).toInt();
    EspUsbDeviceBootKeyboardReport report; report.keys[0] = value;
    const bool ok = keyboard ? keyboard->sendReport(report, 0) : mouse->move(value, -int(value), 7, 1, 0);
    Serial.printf("SENT %u\r\n", unsigned(ok));
  } else if (command.startsWith("wrong ")) {
    uint8_t bytes[32] = {};
    const size_t length = command.substring(6).toInt();
    auto *function = keyboard ? static_cast<EspUsbHidFunction *>(keyboard) : static_cast<EspUsbHidFunction *>(mouse);
    Serial.printf("REJECTED %u\r\n", unsigned(length <= sizeof(bytes) && !function->sendHidInput(0, bytes, length, 0) && device.lastError() == ESP_ERR_INVALID_SIZE));
  } else if (command == "stop") {
    Serial.println(device.end() == ESP_OK ? "STOPPED" : "STOP_FAILED");
  }
}
