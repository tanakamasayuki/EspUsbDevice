// A Keyboard on an interface of its own next to HID Functions that share one.
#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
EspUsbDevice device;
EspUsbDeviceHidKeyboard *keyboard = nullptr;
EspUsbDeviceHidMouse *mouse = nullptr;
EspUsbDeviceHidConsumerControl *consumer = nullptr;
void release() {
  delete keyboard; keyboard = nullptr; delete mouse; mouse = nullptr; delete consumer; consumer = nullptr;
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ")) {
    // merged: Keyboard (own) first, then Mouse and Consumer sharing interface 0.
    // single: Mouse alone on the shared side, then Keyboard (own).
    // shared: all three share one interface, as before this option existed.
    const String layout = command.substring(6);
    release();
    if (layout == "merged") keyboard = new EspUsbDeviceHidKeyboard(device);
    mouse = new EspUsbDeviceHidMouse(device);
    if (layout != "single") consumer = new EspUsbDeviceHidConsumerControl(device);
    if (layout != "merged") keyboard = new EspUsbDeviceHidKeyboard(device);
    const bool own = layout != "shared";
    bool ok = keyboard->enableOwnInterface(own) == ESP_OK && keyboard->ownInterfaceEnabled() == own;
    EspUsbDeviceConfig config; config.product = "v3 HID own interface";
    ok &= device.begin(config);
    // Changing the layout while started is refused and changes nothing.
    ok &= keyboard->enableOwnInterface(!own) == ESP_ERR_INVALID_STATE && keyboard->ownInterfaceEnabled() == own;
    Serial.printf("STARTED %u keyboard_id=%u mouse_id=%u consumer_id=%u\r\n", unsigned(ok),
                  unsigned(keyboard->compositeReportId()), unsigned(mouse->compositeReportId()),
                  unsigned(consumer ? consumer->compositeReportId() : 0));
  } else if (command.startsWith("key ")) {
    EspUsbDeviceBootKeyboardReport report; report.keys[0] = uint8_t(command.substring(4).toInt());
    Serial.printf("SENT %u\r\n", unsigned(keyboard->sendReport(report, 0)));
  } else if (command.startsWith("mouse ")) {
    const int value = command.substring(6).toInt();
    Serial.printf("SENT %u\r\n", unsigned(mouse->move(int8_t(value), int8_t(-value), 7, 1, 0)));
  } else if (command.startsWith("consumer ")) {
    Serial.printf("SENT %u\r\n", unsigned(consumer && consumer->sendUsage(uint16_t(command.substring(9).toInt()), 0)));
  } else if (command == "state") {
    Serial.printf("STATE leds=%u protocol=%u\r\n", unsigned(keyboard->ledState().leds),
                  unsigned(keyboard->protocol()));
  } else if (command == "stop") {
    const bool ok = device.end() == ESP_OK;
    release();
    Serial.println(ok && heap_caps_check_integrity_all(true) ? "STOPPED" : "STOP_FAILED");
  }
}
