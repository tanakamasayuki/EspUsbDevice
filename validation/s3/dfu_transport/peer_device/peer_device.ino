#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
#include <esp_heap_caps.h>
EspUsbDevice device;
EspUsbDeviceDfu *dfu[3] = {};
uint8_t external[65537];
struct Counts { std::atomic<unsigned> downloads{0}, manifests{0}, aborts{0}, errors{0}, bad{0}; } counts[2];
std::atomic<unsigned> detached{0}, inject{0};
bool metadata() {
  EspUsbDevice local; EspUsbDeviceDfu f(local); EspUsbDfuConfig c;
  bool ok = f.transferCapacity() == 0;
  for (size_t n : {size_t(0),size_t(63),size_t(65536),SIZE_MAX}) {
    c.transferBytes = n; ok &= f.configure(c) == ESP_ERR_INVALID_SIZE && f.transferSize() == 1024;
  }
  c = {}; c.downloadPollMs = 0x1000000;
  ok &= f.configure(c) == ESP_ERR_INVALID_SIZE;
  EspUsbDfuBackend b; b.download = [](uint16_t,const uint8_t *,size_t) { return EspUsbDfuStatus::Ok; };
  ok &= f.setBackend(b) == ESP_ERR_INVALID_ARG;
  EspUsbDeviceDfu runtime(local, EspUsbDeviceDfuMode::Runtime);
  b.manifest = []() { return EspUsbDfuStatus::Ok; };
  ok &= runtime.setBackend(b) == ESP_ERR_INVALID_ARG;
  uint8_t external[131]; c = {}; c.transferBytes = 129; c.transferBuffer = external + 1;
  ok &= f.configure(c) == ESP_OK;
  EspUsbDeviceConfig identity; identity.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
  for (unsigned i = 0; i < 3; ++i) {
    ok &= local.begin(identity) && f.transferCapacity() == 129 && !runtime.transferCapacity();
    ok &= local.end() == ESP_OK && !f.transferCapacity();
  }
  return ok;
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "metadata") {
    bool ok = metadata(); const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    for (unsigned i = 0; i < 5 && ok; ++i) ok &= metadata();
    Serial.printf("METADATA ok=%u before=%u after=%u\r\n",unsigned(ok),unsigned(before),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  } else if (command.startsWith("start ")) {
    unsigned size, ext, tolerant = 1; sscanf(command.c_str(), "start %u %u %u", &size, &ext, &tolerant);
    memset(external, 0xa5, sizeof(external));
    for (auto *&f : dfu) { delete f; f = nullptr; }
    bool ok = true;
    for (unsigned j = 0; j < 2; ++j) {
      auto &c = counts[j]; c.downloads = c.manifests = c.aborts = c.errors = c.bad = 0;
      dfu[j] = new EspUsbDeviceDfu(device);
      EspUsbDfuConfig config; config.transferBytes = j ? 65 : size;
      config.downloadPollMs = 7; config.manifestPollMs = 11; config.manifestationTolerant = tolerant;
      if (!j && ext) config.transferBuffer = external + 1;
      EspUsbDfuBackend backend;
      backend.download = [j](uint16_t block, const uint8_t *data, size_t n) {
        ++counts[j].downloads;
        for (size_t i = 0; i < n; ++i) if (data[i] != uint8_t((block * 17u + i) % 251)) ++counts[j].bad;
        return inject.load() == 1 ? EspUsbDfuStatus::Write : EspUsbDfuStatus::Ok;
      };
      backend.manifest = [j]() { ++counts[j].manifests; return inject.load() == 2 ? EspUsbDfuStatus::Verify : EspUsbDfuStatus::Ok; };
      backend.abort = [j]() { ++counts[j].aborts; };
      ok &= dfu[j]->configure(config) == ESP_OK && dfu[j]->setBackend(backend) == ESP_OK;
      ok &= dfu[j]->onError([j](EspUsbDfuStatus) { ++counts[j].errors; }) == ESP_OK;
    }
    detached = inject = 0;
    dfu[2] = new EspUsbDeviceDfu(device, EspUsbDeviceDfuMode::Runtime);
    ok &= dfu[2]->setDetachHandler([]() { ++detached; }) == ESP_OK;
    ok &= device.begin();
    ok &= dfu[0]->transferCapacity() == size && dfu[1]->transferCapacity() == 65 && !dfu[2]->transferCapacity();
    ok &= dfu[0]->configure({}) == ESP_ERR_INVALID_STATE && dfu[0]->setBackend({}) == ESP_ERR_INVALID_STATE;
    ok &= dfu[0]->restartWhenComplete(false) == ESP_ERR_INVALID_STATE && dfu[0]->onProgress({}) == ESP_ERR_INVALID_STATE;
    auto *intruder = new EspUsbDeviceDfu(device);
    // Refused at registration while the device runs; its lifecycle is not public.
    ok &= device.lastError() == ESP_ERR_INVALID_STATE;
    delete intruder;
    Serial.printf("STARTED %u\r\n", unsigned(ok));
  } else if (command.startsWith("inject ")) { inject = command.substring(7).toInt(); Serial.println("INJECTED");
  } else if (command == "stats") {
    for (unsigned j = 0; j < 2; ++j) { auto &c = counts[j];
      Serial.printf("COUNTS %u written=%u downloads=%u manifests=%u aborts=%u errors=%u bad=%u\r\n", j, unsigned(dfu[j]->written()), unsigned(c.downloads.load()), unsigned(c.manifests.load()), unsigned(c.aborts.load()), unsigned(c.errors.load()), unsigned(c.bad.load()));
    }
    Serial.printf("DETACH %u\r\n", unsigned(detached.load()));
  } else if (command == "stop") { Serial.println(device.end() == ESP_OK && external[0] == 0xa5 && external[65536] == 0xa5 ? "STOPPED" : "STOP_FAILED"); }
}
