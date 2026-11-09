#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
#include "esp_heap_caps.h"
// Device side: Download-mode DFU with a RAM backend plus a Runtime interface.
EspUsbDevice device;
EspUsbDeviceDfu dfu(device), runtime(device, EspUsbDeviceDfuMode::Runtime);
std::atomic<unsigned> blocks{0}, manifests{0}, aborts{0}, bad{0}, detached{0};
uint8_t storage[4096 + 2]; size_t stored = 0;
const unsigned LENGTHS[] = {1, 63, 64, 65, 127, 128, 129, 512, 1024, 64};
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  EspUsbDfuBackend backend;
  backend.download = [](uint16_t block, const uint8_t *data, size_t n) {
    ++blocks;
    if (stored + n > sizeof(storage) - 2) return EspUsbDfuStatus::Write;
    for (size_t i = 0; i < n; ++i) if (data[i] != uint8_t((block * 17u + i) % 251)) ++bad;
    memcpy(storage + 1 + stored, data, n); stored += n; return EspUsbDfuStatus::Ok;
  };
  backend.manifest = []() { ++manifests; return EspUsbDfuStatus::Ok; };
  backend.abort = []() { ++aborts; };
  dfu.setBackend(backend); runtime.setDetachHandler([]() { ++detached; });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    blocks = 0; manifests = 0; aborts = 0; bad = 0; detached = 0; stored = 0; memset(storage, 0xc3, sizeof(storage));
    EspUsbDfuConfig c; c.transferBytes = 1024; c.downloadPollMs = 2; c.manifestPollMs = 3; c.manifestationTolerant = true;
    bool ok = dfu.configure(c) == ESP_OK;
    c.detachTimeoutMs = 500; ok &= runtime.configure(c) == ESP_OK;
    EspUsbDeviceConfig dc; dc.vid = 0x1209; dc.pid = 0x0004; dc.product = "EspUsbDevice DFU"; dc.controller = EspUsbController::HighSpeed;
    ok &= device.begin(dc);
    Serial.printf("STARTED ok=%u capacity=%u\r\n", unsigned(ok), unsigned(dfu.transferCapacity()));
  } else if (command == "verify") {
    bool ok = true; size_t at = 0;
    for (unsigned k = 0; k < sizeof(LENGTHS) / sizeof(LENGTHS[0]); ++k) {
      const unsigned block = k == 9 ? 0 : k;  // the last download starts a new image at block 0
      for (size_t i = 0; i < LENGTHS[k]; ++i) ok &= storage[1 + at++] == uint8_t((block * 17u + i) % 251);
    }
    ok &= at == stored && storage[0] == 0xc3;
    for (size_t i = stored + 1; i < sizeof(storage); ++i) ok &= storage[i] == 0xc3;
    Serial.printf("VERIFY ok=%u blocks=%u bytes=%u manifests=%u aborted=%u bad=%u detach=%u\r\n", unsigned(ok), blocks.load(),
                  unsigned(stored), manifests.load(), unsigned(aborts >= 1), bad.load(), detached.load());
  } else if (command == "stop") {
    const bool stopped = !device.started() || device.end() == ESP_OK;
    Serial.printf("STOPPED %u capacity=%u heap=%u\r\n", unsigned(stopped), unsigned(dfu.transferCapacity()),
                  unsigned(heap_caps_check_integrity_all(true)));
  }
}
