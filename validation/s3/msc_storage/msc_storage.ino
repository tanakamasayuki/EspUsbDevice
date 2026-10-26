#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
std::atomic<uint8_t> address{0};
uint8_t buffer[8 * 512], expected[8 * 512];
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &d) { if (d.vid == 0x303a && d.pid == 0x4000) address.store(d.address); });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    address.store(0); bool ok = host.begin(); const uint32_t until = millis() + 10000;
    while (ok && (!address.load() || !host.mscReady(address.load())) && int32_t(until - millis()) > 0) delay(1);
    EspUsbHostMscInquiry inquiry; uint32_t count = 0, size = 0; uint8_t lun = 255;
    ok &= address.load() && host.mscInquiry(inquiry, address.load()) && strncmp(inquiry.vendor, "V3TEST", 6) == 0;
    ok &= host.mscCapacity(count, size, address.load()) && count == 32 && size == 512;
    ok &= host.mscMaxLun(lun, address.load()) && !lun;
    Serial.printf("HOST_READY %u\r\n", unsigned(ok));
  } else if (command == "roundtrip") {
    bool ok = true;
    for (unsigned blocks : {1u, 2u, 7u, 8u}) {
      for (unsigned i = 0; i < blocks * 512; ++i) expected[i] = uint8_t(i * 37 + blocks);
      ok &= host.mscWriteBlocks(3, expected, blocks, address.load());
      memset(buffer, 0, sizeof(buffer));
      ok &= host.mscReadBlocks(3, buffer, blocks, address.load()) && !memcmp(buffer, expected, blocks * 512);
    }
    ok &= host.mscSynchronizeCache(address.load());
    Serial.printf("ROUNDTRIP %u\r\n", unsigned(ok));
  } else if (command.startsWith("error ")) {
    unsigned mode = command.substring(6).toInt(); bool ok = false;
    if (mode == 0) ok = !host.mscWriteBlocks(3, expected, 1, address.load());
    if (mode == 1) ok = !host.mscTestUnitReady(address.load());
    if (mode == 2) ok = !host.mscReadBlocks(3, buffer, 1, address.load());
    if (mode != 1) { Serial.printf("ERROR_REJECTED %u\r\n", unsigned(ok)); return; }
    EspUsbHostMscSense sense;
    const uint8_t key = mode == 0 ? 7 : mode == 1 ? 2 : 3;
    const uint8_t asc = mode == 0 ? 0x27 : mode == 1 ? 0x3a : 0x11;
    ok &= host.mscLastSense(sense, address.load()) && sense.senseKey == key && sense.additionalSenseCode == asc;
    Serial.printf("ERROR_SENSE %u key=%u asc=%u\r\n", unsigned(ok), unsigned(sense.senseKey), unsigned(sense.additionalSenseCode));
  } else if (command == "read") {
    Serial.printf("READ %u\r\n", unsigned(host.mscReadBlocks(3, buffer, 1, address.load())));
  } else if (command == "ready") {
    Serial.printf("READY %u\r\n", unsigned(host.mscTestUnitReady(address.load())));
  } else if (command == "stop") {
    host.end(); Serial.println(!host.ready() ? "STOPPED" : "STOP_FAILED");
  }
}
