#include <Arduino.h>
#include <EspUsbDevice.h>
#include "esp_heap_caps.h"
#include <atomic>
EspUsbDevice device;
EspUsbDeviceHidVendor *hid = nullptr;
EspUsbDeviceHidKeyboard *keyboard = nullptr;
EspUsbDeviceHidMouse *mouse = nullptr;
unsigned reportBytes = 0;
std::atomic<unsigned> outputs{0}, features{0};
std::atomic<bool> payloadOk{true};
std::atomic<unsigned> protocolChanges{0};
uint8_t payload[1023];
bool matches(const uint8_t *data, size_t length) {
  if (length != reportBytes) return false;
  for (size_t i = 0; i < length; ++i) if (data[i] != uint8_t(i * 37 + reportBytes)) return false;
  return true;
}
const uint8_t validDescriptor[] = {0x06,0x00,0xff,0x09,0x01,0xa1,0x01,0x75,0x08,0x95,0x01,0x09,0x01,0x81,0x02,0xc0};
class FailingHid : public EspUsbDeviceHidCustom {
public:
  explicit FailingHid(EspUsbDevice &device) : EspUsbDeviceHidCustom(device, validDescriptor, sizeof(validDescriptor), 1) {}
  bool begin() override { EspUsbDeviceHidCustom::begin(); return false; }
  esp_err_t startError() const override { return ESP_FAIL; }
};
class ShortBoot : public EspUsbDeviceHidCustom {
public:
  ShortBoot(EspUsbDevice &device, uint8_t protocol) : EspUsbDeviceHidCustom(device, validDescriptor, sizeof(validDescriptor), 1), protocol_(protocol) {}
  uint16_t configurationDescriptor(uint8_t *dst, uint8_t itf, uint8_t ep, uint16_t packet) override {
    const auto bytes = EspUsbDeviceHidCustom::configurationDescriptor(dst, itf, ep, packet);
    if (dst && bytes) { dst[6] = 1; dst[7] = protocol_; }
    return bytes;
  }
private:
  uint8_t protocol_;
};
bool parserChecks() {
  bool ok = true;
  const uint8_t bad[][12] = {
    {0x75}, {0xb4}, {0xa4}, {0x85,0}, {0x86,0,1},
    {0xfe,8,1}, {0x75,8,0x97,0xff,0xff,0xff,0xff,0x81,2},
    {0x75,8,0x95,1,0x81,2,0x85,1,0x81,2}
  };
  const uint8_t lengths[] = {1,1,1,2,3,3,9,10};
  for (unsigned i = 0; i < sizeof(lengths); ++i) {
    EspUsbDevice local; EspUsbDeviceHidCustom function(local, bad[i], lengths[i], 1);
    EspUsbDeviceConfig config; config.startTinyUsb = false;
    ok &= !local.begin(config) && local.lastError() != ESP_OK;
    ok &= local.end() == ESP_OK && !function.inputReportCapacity();
  }
  {
    EspUsbDevice local; EspUsbDeviceHidCustom first(local, validDescriptor, sizeof(validDescriptor), 1);
    auto *failure = new FailingHid(local);
    EspUsbDeviceConfig config; config.startTinyUsb = false;
    ok &= !local.begin(config) && local.lastError() == ESP_FAIL && !first.inputReportCapacity();
    delete failure;
    ok &= local.begin(config) && first.inputReportCapacity() == 1 && !first.outputReportCapacity() && !first.featureReportCapacity();
    ok &= local.end() == ESP_OK;
  }
  for (uint8_t protocol : {1, 2}) {
    EspUsbDevice local; ShortBoot function(local, protocol);
    EspUsbDeviceConfig config; config.startTinyUsb = false;
    ok &= !local.begin(config) && local.lastError() == ESP_ERR_INVALID_SIZE;
    ok &= local.end() == ESP_OK;
  }
  return ok;
}
bool metadata() {
  bool ok = true;
  const uint16_t sizes[] = {1,63,64,65,127,128,129,255,256,257,511,1023};
  for (auto size : sizes) {
    EspUsbDevice local; EspUsbDeviceHidVendor function(local, size);
    EspUsbDeviceConfig config; config.startTinyUsb = false;
    ok &= local.begin(config) && function.startError() == ESP_OK;
    ok &= function.inputReportCapacity() == size_t(size) + 1 && function.outputReportCapacity() == size_t(size) + 1 && function.featureReportCapacity() == size_t(size) + 1;
    ok &= local.end() == ESP_OK && !function.inputReportCapacity() && !function.outputReportCapacity() && !function.featureReportCapacity();
  }
  {
    const uint8_t maximum[] = {0x06,0x00,0xff,0x09,0x01,0xa1,0x01,0x75,0x08,0x96,0xff,0xff,0x09,0x01,0x81,0x02,0xc0};
    EspUsbDevice local; EspUsbDeviceHidCustom function(local, maximum, sizeof(maximum), 64);
    EspUsbDeviceConfig config; config.startTinyUsb = false;
    ok &= local.begin(config) && function.inputReportCapacity() == 65535 && !function.outputReportCapacity() && !function.featureReportCapacity();
    ok &= local.end() == ESP_OK;
  }
  {
    EspUsbDevice local; EspUsbDeviceHidKeyboard function(local);
    EspUsbDeviceConfig config; config.startTinyUsb = false;
    ok &= function.enableNkro(true) == ESP_OK && function.onProtocol(nullptr) == ESP_OK && function.onOutputReport(nullptr) == ESP_OK;
    ok &= local.begin(config) && function.inputReportCapacity() == 29;
    ok &= function.enableNkro(false) == ESP_ERR_INVALID_STATE && function.nkroEnabled();
    ok &= function.onProtocol(nullptr) == ESP_ERR_INVALID_STATE && function.onOutputReport(nullptr) == ESP_ERR_INVALID_STATE;
    ok &= local.end() == ESP_OK && function.enableNkro(false) == ESP_OK;
    ok &= local.begin(config) && function.inputReportCapacity() == 8;
    ok &= local.end() == ESP_OK;
  }
  return ok && parserChecks();
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "metadata") {
    bool ok = metadata();
    const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    for (unsigned i = 0; i < 5 && ok; ++i) ok &= metadata();
    const size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    Serial.printf("METADATA ok=%u before=%u after=%u\r\n", unsigned(ok), unsigned(before), unsigned(after));
  } else if (command.startsWith("start ")) {
    delete hid; hid = nullptr; delete keyboard; keyboard = nullptr;
    reportBytes = command.substring(6).toInt();
    hid = new EspUsbDeviceHidVendor(device, reportBytes);
    hid->onOutputReport([](const EspUsbDeviceHidReport &r) { payloadOk.store(payloadOk.load() && matches(r.data, r.length)); ++outputs; });
    hid->onFeatureReport([](const EspUsbDeviceHidReport &r) { payloadOk.store(payloadOk.load() && matches(r.data, r.length)); ++features; });
    outputs.store(0); features.store(0); payloadOk = true;
    EspUsbDeviceConfig config; config.product = "v3 HID transport";
    const bool ok = device.begin(config);
    for (unsigned i = 0; i < reportBytes && i < sizeof(payload); ++i) payload[i] = uint8_t(i * 37 + reportBytes);
    Serial.printf("STARTED %u in=%u out=%u feature=%u\r\n", unsigned(ok), unsigned(hid->inputReportCapacity()), unsigned(hid->outputReportCapacity()), unsigned(hid->featureReportCapacity()));
  } else if (command == "send") {
    const bool rejected = !hid->sendInput(payload, reportBytes + 1, 0) && !hid->sendHidInput(255, payload, reportBytes, 0) && !hid->sendHidInput(6, payload, SIZE_MAX, 0);
    Serial.printf("SENT %u rejected=%u\r\n", unsigned(hid->sendInput(payload, reportBytes)), unsigned(rejected));
  } else if (command == "result") {
    const uint32_t deadline = millis() + 3000;
    while ((outputs < 3 || features < 1) && int32_t(deadline - millis()) > 0) delay(1);
    Serial.printf("OUTPUTS %u features=%u payload=%u\r\n", outputs.load(), features.load(), unsigned(payloadOk.load()));
  } else if (command == "keyboard") {
    delete hid; hid = nullptr;
    keyboard = new EspUsbDeviceHidKeyboard(device); keyboard->enableNkro(true);
    keyboard->onProtocol([](const EspUsbDeviceHidProtocolEvent &) { ++protocolChanges; });
    EspUsbDeviceConfig config; config.product = "v3 HID transport";
    Serial.printf("KEYBOARD_STARTED %u\r\n", unsigned(device.begin(config)));
  } else if (command == "protocol") {
    Serial.printf("PROTOCOL %u leds=%u\r\n", unsigned(keyboard->protocol()), unsigned(keyboard->ledState().leds));
  } else if (command == "key") {
    EspUsbDeviceBootKeyboardReport report; report.keys[0] = 4;
    Serial.printf("KEY_SENT %u\r\n", unsigned(keyboard->sendReport(report)));
  } else if (command == "mouse") {
    mouse = new EspUsbDeviceHidMouse(device);
    EspUsbDeviceConfig config; config.product = "v3 HID transport";
    Serial.printf("MOUSE_STARTED %u\r\n", unsigned(device.begin(config)));
  } else if (command == "move") {
    Serial.printf("MOVE_SENT %u\r\n", unsigned(mouse->move(2, 3, 4, 1)));
  } else if (command == "drop") {
    delete hid; hid = nullptr;
    Serial.printf("DROPPED %u\r\n", unsigned(!device.ready()));
  } else if (command == "drop_keyboard") {
    const unsigned before = protocolChanges.load();
    delete keyboard; keyboard = nullptr;
    Serial.printf("KEYBOARD_DROPPED %u callbacks=%u\r\n", unsigned(!device.ready()), unsigned(protocolChanges.load() > before));
  } else if (command == "stop") {
    Serial.println(device.end() == ESP_OK ? "STOPPED" : "STOP_FAILED");
  }
}
