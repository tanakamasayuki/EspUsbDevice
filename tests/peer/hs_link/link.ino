#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
// Host on the HS port of one board of the 2-3/2-4 pair: which direction
// enumerates, at what speed, and what the peer Device reports.
EspUsbHost host;
std::atomic<unsigned> connected{0}, vid{0}, pid{0}, speed{255};
bool started = false;
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    vid = info.vid; pid = info.pid; speed = info.speed; ++connected;
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    connected = 0; vid = 0; pid = 0; speed = 255;
    EspUsbHostConfig config; config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
    started = host.begin(config);
    Serial.printf("HOST_STARTED %u\r\n", unsigned(started));
  } else if (command == "wait") {
    const uint32_t begun = millis();
    while (!connected.load() && millis() - begun < 5000) delay(1);
    Serial.printf("DEVICE n=%u vid=%04x pid=%04x speed=%u\r\n", connected.load(), vid.load(), pid.load(), speed.load());
  } else if (command == "stop") {
    if (started) host.end();
    started = false;
    Serial.println("HOST_STOPPED");
  }
}
