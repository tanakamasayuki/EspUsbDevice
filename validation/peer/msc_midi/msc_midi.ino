#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"
// Host side: MSC BOT on a RAM disk and two-cable MIDI echo over a high-speed link.
EspUsbHost host;
std::atomic<unsigned> address{0}, speed{255}, messages{0}, bad{0};
uint8_t sent[7 * 512], received[7 * 512];
bool started = false;
unsigned sectorSize = 512;
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (strcmp(info.product, "EspUsbDevice MSC MIDI") == 0) { speed = info.speed; address = info.address; }
  });
  host.onMidiMessage([](const EspUsbHostMidiMessage &event) {
    const unsigned i = messages.load();
    const uint8_t expected[] = {uint8_t((i % 2) << 4 | 9), uint8_t(0x90 | i % 16), uint8_t(i % 128), uint8_t((i * 3 + 7) % 128)};
    if (!event.raw || event.length != 4 || memcmp(expected, event.raw, 4)) ++bad;
    ++messages;
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ")) {
    sectorSize = command.substring(6).toInt();
    address = 0; speed = 255;
    EspUsbHostConfig config; config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
    started = host.begin(config);
    const uint32_t begun = millis();
    while (started && (!address.load() || !host.mscReady(address.load()) || !host.midiReady(address.load())) && millis() - begun < 12000) delay(1);
    uint32_t count = 0, size = 0;
    const bool ok = started && address.load() && host.midiReady(address.load()) && host.mscCapacity(count, size, address.load());
    Serial.printf("HOST_READY ok=%u speed=%u blocks=%u size=%u\r\n", unsigned(ok), speed.load(), unsigned(count), unsigned(size));
  } else if (command == "exercise") {
    bool ok = address.load(); unsigned writes = 0, reads = 0;
    for (unsigned lba : {0u, 3u, 57u}) {
      for (size_t i = 0; i < 7 * sectorSize; ++i) sent[i] = uint8_t(i * 13 + lba + sectorSize);
      ok &= host.mscWriteBlocks(lba, sent, 7, address.load());
      if (ok) ++writes;
      memset(received, 0, sizeof(received));
      ok &= host.mscReadBlocks(lba, received, 7, address.load()) && !memcmp(received, sent, 7 * sectorSize);
      if (ok) ++reads;
    }
    ok &= host.mscSynchronizeCache(address.load());
    messages = 0; bad = 0;
    for (unsigned i = 0; i < 32 && ok; ++i) {
      const uint8_t packet[] = {uint8_t((i % 2) << 4 | 9), uint8_t(0x90 | i % 16), uint8_t(i % 128), uint8_t((i * 3 + 7) % 128)};
      ok &= host.midiSend(packet, sizeof(packet), address.load());
      const uint32_t begun = millis();
      while (ok && messages.load() < i + 1 && millis() - begun < 2000) delay(1);
      ok &= messages.load() == i + 1 && bad.load() == 0;
    }
    Serial.printf("CLASSES ok=%u writes=%u reads=%u midi=%u bad=%u\r\n", unsigned(ok), writes, reads, messages.load(), bad.load());
  } else if (command == "stop") {
    if (started) host.end();
    started = false; address = 0;
    Serial.printf("STOPPED heap=%u\r\n", unsigned(heap_caps_check_integrity_all(true)));
  }
}
