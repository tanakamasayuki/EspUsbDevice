#include <Arduino.h>
#include <EspUsbDevice.h>
#include "esp_heap_caps.h"
// A FAT RAM disk in PSRAM, sized so the cluster grows with it: the PC reads the
// whole volume back and checks it with fsck.fat and its own FAT12 reader.
EspUsbDevice device;
EspUsbDeviceMsc msc(device);
EspUsbDeviceMscFatRamDisk *disk = nullptr;
uint8_t *storage = nullptr;
constexpr size_t fileBytes = 300 * 1024;

uint8_t pattern(size_t i) { return uint8_t(i * 7 + (i >> 9)); }
void release() {
  if (device.started()) device.end();
  delete disk; disk = nullptr;
  heap_caps_free(storage); storage = nullptr;
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ")) {
    release();
    const size_t bytes = size_t(command.substring(6).toInt()) * 1024;
    storage = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    bool ok = storage != nullptr;
    if (ok) {
      disk = new EspUsbDeviceMscFatRamDisk(storage, bytes);
      auto *data = static_cast<uint8_t *>(heap_caps_malloc(fileBytes, MALLOC_CAP_SPIRAM));
      ok &= data && disk->format("ESPUSB");
      for (size_t i = 0; ok && i < fileBytes; ++i) data[i] = pattern(i);
      ok &= ok && disk->addFile("BIG.BIN", data, fileBytes) && disk->addTextFile("README.TXT", "EspUsbDevice FAT12\r\n");
      // The device reads its own file back through the FAT chain as well.
      if (ok) { memset(data, 0, fileBytes); ok &= disk->readFile("BIG.BIN", data, fileBytes) == fileBytes; }
      for (size_t i = 0; ok && i < fileBytes; ++i) ok &= data[i] == pattern(i);
      heap_caps_free(data);
      ok &= ok && disk->attach(msc);
      msc.mediaPresent(true);
      EspUsbMscConfig mc; mc.transferBytes = 4096; ok &= msc.configure(mc) == ESP_OK;
      EspUsbDeviceConfig identity; identity.vid = 0x1209; identity.pid = 0x0001;
      identity.product = "EspUsbDevice MSC FAT"; identity.serialNumber = "espusb-msc-fat";
      ok &= ok && device.begin(identity);
    }
    Serial.printf("STARTED ok=%u blocks=%u\r\n", unsigned(ok), unsigned(disk ? disk->blockCount() : 0));
  } else if (command == "stop") {
    release();
    Serial.printf("STOPPED heap=%u\r\n", unsigned(heap_caps_check_integrity_all(true)));
  }
}
