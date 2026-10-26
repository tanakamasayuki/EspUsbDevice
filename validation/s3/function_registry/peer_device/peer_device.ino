#include <Arduino.h>
#include <EspUsbDevice.h>
#include "esp_heap_caps.h"

// A control-only descriptor Function: no driver/endpoint is needed for the
// descriptor-only metadata checks. Every writer respects its destination size.
class NamedControl : public EspUsbDeviceClass {
public:
  NamedControl(EspUsbDevice &device, const char *name) : EspUsbDeviceClass(device), name_(name) {}
  bool isHid() const override { return false; }
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 0; }
  const char *functionName() const override { return name_; }
  void assignFunctionIds(uint8_t, uint8_t string) override { string_ = string; }
  uint16_t configurationDescriptor(uint8_t *dst, uint8_t itf, uint8_t, uint16_t) override {
    const uint8_t bytes[] = {9, 4, itf, 0, 0, 0xff, 0, 0, string_};
    if (dst) memcpy(dst, bytes, sizeof(bytes)); return sizeof(bytes);
  }
  uint16_t configurationDescriptorForSpeed(uint8_t *dst, size_t capacity, uint8_t itf, uint8_t ep, bool) override {
    return !dst || capacity >= 9 ? configurationDescriptor(dst, itf, ep, 64) : 0;
  }
  bool checkString(EspUsbDevice &device) const {
    const auto *text = device.stringDescriptor(string_, 0x409);
    if (!text || uint8_t(text[0]) != 2 + 2 * strlen(name_)) return false;
    for (size_t i = 0; i < strlen(name_); ++i) if (text[i + 1] != uint8_t(name_[i])) return false;
    return true;
  }
private:
  const char *name_;
  uint8_t string_ = 0;
};

bool metadataChecks() {
  EspUsbDevice device;
  NamedControl *items[7];
  for (auto &item : items) item = new NamedControl(device, "function");
  EspUsbDeviceConfig config; config.startTinyUsb = false;
  bool ok = device.begin(config);
  const auto *descriptor = device.configurationDescriptor(0);
  ok &= descriptor && descriptor[4] == 7 && (descriptor[2] | unsigned(descriptor[3]) << 8) == 72;
  for (auto *item : items) ok &= item->checkString(device);
  auto *late = new NamedControl(device, "late");
  ok &= device.lastError() == ESP_ERR_INVALID_STATE;
  ok &= device.end() == ESP_OK;
  ok &= !device.deviceDescriptor() && device.lastError() == ESP_ERR_INVALID_STATE;
  ok &= !device.begin(config) && device.lastError() == ESP_ERR_INVALID_STATE;
  // Explicit retry after stopping must recover registration, not leave a
  // silently omitted Function. Re-registering an existing entry is idempotent.
  ok &= device.addClass(late) && device.addClass(late);
  ok &= device.begin(config);
  descriptor = device.configurationDescriptor(0);
  ok &= descriptor && descriptor[4] == 8 && late->checkString(device);
  device.end();
  delete items[3]; items[3] = new NamedControl(device, "tail");
  ok &= device.begin(config) && items[3]->checkString(device);
  descriptor = device.configurationDescriptor(0);
  ok &= descriptor && descriptor[4] == 8;
  device.end();
  delete late;
  for (auto *item : items) delete item;
  ok &= device.begin(config);
  descriptor = device.configurationDescriptor(0);
  ok &= descriptor && descriptor[4] == 0 && descriptor[2] == 9;
  device.end();
  {
    EspUsbDevice bounded;
    NamedControl *many[252];
    char longest[127]; memset(longest, 'x', 126); longest[126] = 0;
    for (auto &item : many) item = new NamedControl(bounded, longest);
    ok &= !bounded.deviceDescriptor() && bounded.lastError() == ESP_ERR_INVALID_SIZE;
    ok &= !bounded.begin(config) && bounded.lastError() == ESP_ERR_INVALID_SIZE;
    delete many[251]; many[251] = nullptr;
    ok &= bounded.begin(config);
    const auto *last = bounded.stringDescriptor(255, 0x409);
    const auto *first = bounded.stringDescriptor(5, 0x409);
    ok &= last && first && last != first && last[0] == 0x03fe && last[126] == 'x';
    ok &= first && first[0] == 0x03fe && first[126] == 'x';
    ok &= bounded.end() == ESP_OK;
    for (auto *item : many) delete item;
  }
  return ok;
}

EspUsbDevice device;
// Vendor-defined one-byte input. The composite builder assigns report IDs.
const uint8_t report[] = {0x06,0x00,0xff,0x09,0x01,0xa1,0x01,0x15,0x00,0x26,0xff,0x00,0x75,0x08,0x95,0x01,0x09,0x01,0x81,0x02,0xc0};
class PreferredHid : public EspUsbDeviceHidCustom {
public:
  PreferredHid(EspUsbDevice &device, uint8_t preferred)
      : EspUsbDeviceHidCustom(device, report, sizeof(report), 1), preferred_(preferred) {}
  uint8_t hidReportId() const override { return preferred_; }
private:
  uint8_t preferred_;
};
PreferredHid *functions[7] = {};
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "metadata") {
    bool ok = metadataChecks();
    const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    for (unsigned i = 0; i < 20 && ok; ++i) ok &= metadataChecks();
    const size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    Serial.printf("METADATA ok=%u before=%u after=%u\r\n", unsigned(ok), unsigned(before), unsigned(after));
  } else if (command == "start") {
    for (unsigned i = 0; i < 7; ++i) functions[i] = new PreferredHid(device, i < 2 ? 5 : 0);
    EspUsbDeviceConfig config; config.product = "v3 Function registry";
    const bool started = device.begin(config);
    Serial.printf("REGISTRY_STARTED %u reports=%u\r\n", unsigned(started), unsigned(device.hidReportDescriptorLength(0)));
  } else if (command == "active_reject") {
    auto *cdc = new EspUsbDeviceCdcSerial(device);
    bool rejected = device.lastError() == ESP_ERR_INVALID_STATE;
    delete cdc;
    rejected &= device.ready();
    auto *vendor = new EspUsbDeviceVendor(device);
    rejected &= device.lastError() == ESP_ERR_INVALID_STATE;
    delete vendor;
    rejected &= device.ready();
    auto *hid = new PreferredHid(device, 5);
    rejected &= device.lastError() == ESP_ERR_INVALID_STATE;
    rejected &= !hid->begin() && hid->startError() == ESP_ERR_INVALID_STATE && !hid->inputReportCapacity();
    const uint8_t data = 0xab;
    rejected &= !hid->sendReport(&data, 1);
    delete hid;
    rejected &= device.ready();
    Serial.printf("ACTIVE_REJECT %u\r\n", unsigned(rejected));
  } else if (command == "send") {
    bool ok = functions[0]->compositeReportId() == 5 && functions[1]->compositeReportId() == 1;
    const uint8_t ignored = 0;
    ok &= !functions[0]->sendReport(&ignored, 1, 1); // Explicit wrong ID is rejected.
    for (unsigned i = 0; i < 7; ++i) {
      const uint8_t data = 0x40 + functions[i]->compositeReportId();
      // Default reportId must follow the composite identity assigned at begin.
      ok &= functions[i]->sendReport(&data, 1);
    }
    Serial.printf("REPORTS_SENT %u\r\n", unsigned(ok));
  } else if (command == "stop") {
    const bool stopped = device.end() == ESP_OK;
    for (auto &function : functions) { delete function; function = nullptr; }
    Serial.println(stopped ? "STOPPED" : "STOP_FAILED");
  }
}
