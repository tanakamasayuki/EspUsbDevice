#include <Arduino.h>
#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceHidGamepad *gamepad = nullptr;
EspUsbDeviceHidConsumerControl *consumer = nullptr;
EspUsbDeviceHidSystemControl *systemControl = nullptr;
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ")) {
    const unsigned mode = command.substring(6).toInt();
    if (mode == 0 || mode == 3) gamepad = new EspUsbDeviceHidGamepad(device);
    if (mode == 1 || mode == 3) consumer = new EspUsbDeviceHidConsumerControl(device);
    if (mode == 2 || mode == 3) systemControl = new EspUsbDeviceHidSystemControl(device);
    EspUsbDeviceConfig config; config.product = "v3 typed HID";
    Serial.printf("STARTED %u\r\n", unsigned(device.begin(config)));
  } else if (command.startsWith("send ")) {
    unsigned kind = 99, phase = 99;
    sscanf(command.c_str(), "send %u %u", &kind, &phase);
    bool ok = false;
    if (kind == 0 && gamepad) {
      if (phase == 0) {
        EspUsbDeviceGamepadReport report;
        report.x = -127; report.y = 127; report.z = -1; report.rz = 1;
        report.rx = -64; report.ry = 64; report.hat = 8; report.buttons = 0x81020408;
        ok = gamepad->sendReport(report);
      } else if (phase == 1) ok = gamepad->send(12, -23, 34, -45, 56, -67, 3, 0xfedcba98);
      else if (phase == 2) ok = gamepad->releaseAll();
    } else if (kind == 1 && consumer) {
      if (phase == 0) ok = consumer->press(0x123) && consumer->usage() == 0x123;
      else if (phase == 1) ok = consumer->sendUsage(0xe9) && consumer->usage() == 0xe9;
      else if (phase == 2) ok = consumer->release() && !consumer->usage();
      else if (phase == 3) ok = consumer->click(0xea, 10) && !consumer->usage();
    } else if (kind == 2 && systemControl) {
      if (phase == 0) ok = systemControl->press(ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF) && systemControl->usage() == ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF;
      else if (phase == 1) ok = systemControl->sendUsage(ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY) && systemControl->usage() == ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY;
      else if (phase == 2) ok = systemControl->release() && !systemControl->usage();
      else if (phase == 3) ok = systemControl->click(ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST, 10) && !systemControl->usage();
    }
    Serial.printf("SENT kind=%u phase=%u ok=%u\r\n", kind, phase, unsigned(ok));
  } else if (command == "invalid_system") {
    const uint8_t before = systemControl->usage();
    bool ok = true;
    for (uint8_t value : {1, 4, 0x80, 0x84, 0xff}) ok &= !systemControl->sendUsage(value, 0) && systemControl->usage() == before;
    ok &= !systemControl->press(1, 0) && systemControl->usage() == before;
    ok &= !systemControl->click(4, 0) && systemControl->usage() == before;
    Serial.printf("INVALID_SYSTEM ok=%u rejected=7 usage=%u\r\n", unsigned(ok), unsigned(systemControl->usage()));
  } else if (command == "stop") {
    bool ok = device.end() == ESP_OK;
    delete gamepad; gamepad = nullptr; delete consumer; consumer = nullptr;
    delete systemControl; systemControl = nullptr;
    Serial.println(ok ? "STOPPED" : "STOP_FAILED");
  }
}
