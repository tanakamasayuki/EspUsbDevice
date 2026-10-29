#include <Arduino.h>
#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceMsc disk(device);
uint8_t guarded[64 * 512 + 2];
EspUsbDeviceMscRamDisk raw(guarded + 1, 64, 511);
EspUsbDeviceMscFatRamDisk fat(guarded + 1, 64 * 512);
uint8_t payload[1537], check[1537];
unsigned mode = 0;
bool guards() { return guarded[0] == 0xc3 && guarded[sizeof(guarded)-1] == 0xc3; }
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ")) {
    unsigned capacity = 0;
    sscanf(command.c_str(), "start %u %u", &mode, &capacity);
    memset(guarded, 0xc3, sizeof(guarded));
    bool ok = true;
    if (mode == 0) {
      raw.clear(0x5a);
      ok &= raw.valid() && raw.byteSize() == 64 * 511 && raw.blockCount() == 64 && raw.blockSize() == 511;
      uint8_t block[511]; memset(block, 0x7b, sizeof(block));
      ok &= raw.writeBlock(63, block) && raw.readBlock(63, check) && !memcmp(block, check, sizeof(block));
      ok &= !raw.readBlock(64, check) && !raw.writeBlock(64, block);
      raw.writeByte(0, 510, 0xa7);
      ok &= raw.attach(disk);
    } else {
      for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = uint8_t(i * 37 + 11);
      ok &= fat.format("V3HELPERS") && fat.addTextFile("README.TXT", "v3 helper\n");
      ok &= fat.addFile("PAYLOAD.BIN", payload, sizeof(payload));
      ok &= !fat.addFile("payload.bin", payload, sizeof(payload));
      ok &= fat.exists("payload.bin") && fat.fileSize("PAYLOAD.BIN") == sizeof(payload);
      ok &= fat.readFile("PAYLOAD.BIN", check, sizeof(check)) == sizeof(check) && !memcmp(payload, check, sizeof(payload));
      ok &= fat.attach(disk);
    }
    EspUsbMscConfig config; config.transferBytes = capacity;
    ok &= disk.configure(config) == ESP_OK;
    disk.mediaPresent(true); disk.isWritable(true);
    EspUsbDeviceConfig identity; identity.product = "v3 MSC helpers";
    ok &= device.begin(identity) && guards();
    Serial.printf("STARTED ok=%u mode=%u capacity=%u\r\n", unsigned(ok), mode, unsigned(disk.transferCapacity()));
  } else if (command == "verify") {
    bool ok = guards();
    if (!mode) {
      for (size_t i = 0; i < 7 * 511; ++i) ok &= raw.data()[3 * 511 + i] == uint8_t(i * 13 + 23);
      ok &= raw.data()[510] == 0xa7;
      for (size_t i = 64 * 511; i < 64 * 512; ++i) ok &= guarded[1 + i] == 0xc3;
    } else {
      ok &= fat.fileSize("payload.bin") == sizeof(check) && fat.readFile("payload.bin", check, sizeof(check)) == sizeof(check);
      for (size_t i = 0; i < sizeof(check); ++i) ok &= check[i] == uint8_t(i * 13 + 23);
      ok &= fat.readFile("README.TXT", check, sizeof(check)) == 10 && !memcmp(check, "v3 helper\n", 10);
    }
    Serial.printf("VERIFIED ok=%u guards=%u\r\n", unsigned(ok), unsigned(guards()));
  } else if (command == "stop") {
    Serial.println(device.end() == ESP_OK && guards() ? "STOPPED" : "STOP_FAILED");
  }
}
