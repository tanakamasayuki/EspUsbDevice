#include <Arduino.h>
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
bool started = false;
uint8_t buffer[129];
size_t expected = 0;
size_t received = 0;
size_t chunk = sizeof(buffer);
bool explicitFlush = false;

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);
}

void loop() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command == "start") {
      EspUsbDeviceConfig config;
      config.product = "v3 Vendor API probe";
      started = device.begin(config);
      Serial.println(started ? "DEVICE_STARTED" : "DEVICE_START_FAILED");
    } else if (command == "status") {
      Serial.printf("DEVICE_READY %u\r\n", unsigned(device.ready()));
    } else if (command.startsWith("expect ")) {
      unsigned length = 0, requestedChunk = 0, flush = 0;
      sscanf(command.c_str(), "expect %u %u %u", &length, &requestedChunk, &flush);
      if (length == 0 || length > sizeof(buffer) || expected) {
        Serial.println("INVALID_EXPECT");
      } else {
        expected = length;
        received = 0;
        chunk = requestedChunk ? requestedChunk : sizeof(buffer);
        explicitFlush = flush != 0;
        Serial.printf("ARMED %u\r\n", unsigned(expected));
      }
    } else if (command == "stop") {
      if (started) device.end();
      started = false;
      expected = received = 0;
      Serial.println("STOPPED");
    }
  }
  if (started && expected) {
    received += vendor.read(buffer + received, expected - received);
    if (received == expected) {
      size_t sent = 0;
      const uint32_t deadline = millis() + 3000;
      while (sent < received && int32_t(deadline - millis()) > 0) {
        const size_t remaining = received - sent;
        sent += vendor.write(buffer + sent, remaining < chunk ? remaining : chunk);
        if (explicitFlush) vendor.flush();
        delay(1);
      }
      if (sent == received) Serial.printf("ECHOED %u\r\n", unsigned(sent));
      else Serial.println("ECHO_FAILED");
      expected = received = 0;
    }
  }
  delay(1);
}
