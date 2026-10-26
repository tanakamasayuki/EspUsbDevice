#include <Arduino.h>
#include <EspUsbDevice.h>
#include "esp_heap_caps.h"

bool writer(const char *label, EspUsbDeviceClass &function, unsigned &cases) {
  for (bool hs : {false, true}) {
    const uint16_t size = function.configurationDescriptorForSpeed(nullptr, 0, 0, 1, hs);
    if (!size) { Serial.printf("WRITER_FAILED %s hs=%u query\r\n", label, unsigned(hs)); return false; }
    uint8_t *storage = static_cast<uint8_t *>(malloc(size + 32));
    if (!storage) return false;
    memset(storage, 0xa5, size + 32);
    bool ok = !function.configurationDescriptorForSpeed(storage + 16, size - 1, 0, 1, hs);
    for (size_t i = 0; i < 16; ++i) ok &= storage[i] == 0xa5;
    for (size_t i = 16 + size - 1; i < size + 32; ++i) ok &= storage[i] == 0xa5;
    memset(storage, 0xa5, size + 32);
    ok &= function.configurationDescriptorForSpeed(storage + 16, size, 0, 1, hs) == size;
    for (size_t i = 0; i < 16; ++i) ok &= storage[i] == 0xa5 && storage[16 + size + i] == 0xa5;
    size_t offset = 0;
    while (ok && offset < size) {
      const uint8_t length = storage[16 + offset];
      ok &= length >= 2 && length <= size - offset;
      if (ok && hs && storage[16 + offset + 1] == 5 && length >= 7 && (storage[16 + offset + 3] & 3) == 2)
        ok &= storage[16 + offset + 4] == 0 && storage[16 + offset + 5] == 2;
      offset += length;
    }
    ok &= offset == size;
    free(storage);
    if (!ok) { Serial.printf("WRITER_FAILED %s hs=%u bounds\r\n", label, unsigned(hs)); return false; }
    ++cases;
  }
  return true;
}

bool writers(unsigned &cases) {
  bool ok = true;
  EspUsbDevice device;
  { EspUsbDeviceVendor f(device); ok &= writer("vendor", f, cases); }
  { EspUsbDeviceCdcSerial f(device); ok &= writer("cdc", f, cases); }
  { EspUsbDeviceCcid f(device); ok &= writer("ccid", f, cases); }
  { EspUsbDeviceNet f(device); ok &= writer("ncm", f, cases); }
  { EspUsbDeviceMsc f(device); ok &= writer("msc", f, cases); }
  { EspUsbDeviceMidi f(device); ok &= writer("midi1", f, cases); }
  { EspUsbDeviceMidi f(device, 16); ok &= writer("midi16", f, cases); }
  { EspUsbDeviceMidi f(device, 16, 1); ok &= writer("midi-asymmetric", f, cases); }
  { EspUsbDeviceDfu f(device); ok &= writer("dfu-download", f, cases); }
  { EspUsbDeviceDfu f(device, EspUsbDeviceDfuMode::Runtime); ok &= writer("dfu-runtime", f, cases); }
  { EspUsbDeviceHidKeyboard f(device); ok &= writer("keyboard", f, cases); }
  { EspUsbDeviceHidMouse f(device); ok &= writer("mouse", f, cases); }
  { EspUsbDeviceHidVendor f(device); ok &= writer("hid-vendor", f, cases); }
  { EspUsbDeviceHidGamepad f(device); ok &= writer("gamepad", f, cases); }
  { EspUsbDeviceHidConsumerControl f(device); ok &= writer("consumer", f, cases); }
  { EspUsbDeviceHidSystemControl f(device); ok &= writer("system", f, cases); }
  for (auto protocol : {EspUsbAudioProtocol::Uac1, EspUsbAudioProtocol::Uac2}) {
    EspUsbAudioFunction f(device, protocol);
    ok &= f.addPlaybackStream().addFormat(EspUsbAudioFormat());
    ok &= writer("audio-playback", f, cases);
    ok &= f.addCaptureStream().addFormat(EspUsbAudioFormat());
    ok &= writer("audio-duplex", f, cases);
  }
  { EspUsbDeviceVideo f(device); ok &= writer("mjpeg", f, cases);
    ok &= f.setFormat(EspUsbDeviceVideoFormat::Yuy2); ok &= writer("yuy2", f, cases); }
  return ok;
}

class BadSize : public EspUsbDeviceClass {
public:
  BadSize(EspUsbDevice &device, bool unstable) : EspUsbDeviceClass(device), unstable_(unstable) {}
  bool isHid() const override { return false; }
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 0; }
  uint16_t configurationDescriptor(uint8_t *dst, uint8_t, uint8_t, uint16_t) override {
    if (!dst) return unstable_ ? (++queries_ == 1 ? 9 : 19) : UINT16_MAX;
    ++writes; return 0;
  }
  unsigned writes = 0;
private:
  bool unstable_; unsigned queries_ = 0;
};
bool badSizes() {
  for (bool unstable : {false, true}) {
    EspUsbDevice device; BadSize function(device, unstable);
    EspUsbDeviceConfig config; config.startTinyUsb = false;
    if (device.begin(config) || device.lastError() != ESP_ERR_INVALID_SIZE || function.writes) return false;
    if (device.bosDescriptor() || device.microsoftOs20Descriptor()) return false;
  }
  return true;
}
class SpeedSized : public EspUsbDeviceClass {
public:
  using EspUsbDeviceClass::EspUsbDeviceClass;
  explicit SpeedSized(EspUsbDevice &device) : EspUsbDeviceClass(device) {}
  bool isHid() const override { return false; }
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 0; }
  uint16_t configurationDescriptor(uint8_t *dst, uint8_t itf, uint8_t, uint16_t packet) override {
    const uint16_t size = packet > 64 ? 18 : 9;
    if (dst) {
      const uint8_t bytes[] = {9, 4, itf, 0, 0, 0xff, 0, 0, 0};
      memcpy(dst, bytes, 9);
      if (size == 18) { memcpy(dst + 9, bytes, 9); dst[12] = 1; }
    }
    return size;
  }
};
bool detached() {
  EspUsbDevice device; EspUsbDeviceCdcSerial serial(device);
  EspUsbDeviceConfig config; config.startTinyUsb = false;
  if (!device.begin(config) || device.bosDescriptor() || device.microsoftOs20Descriptor()) return false;
  const uint8_t *fs = device.configurationDescriptor(0);
  const uint8_t *hs = device.configurationDescriptorForSpeed(0, true);
  if (!fs || !hs || fs == hs || fs[2] != 75 || hs[2] != 75) return false;
  bool full = false, high = false;
  for (size_t i = 9; i < 75; i += fs[i]) if (fs[i + 1] == 5 && (fs[i + 3] & 3) == 2) full |= fs[i + 4] == 64 && fs[i + 5] == 0;
  for (size_t i = 9; i < 75; i += hs[i]) if (hs[i + 1] == 5 && (hs[i + 3] & 3) == 2) high |= hs[i + 4] == 0 && hs[i + 5] == 2;
  bool ok = full && high && device.end() == ESP_OK;
  config.webusbEnabled = true;
  ok &= device.begin(config) && device.bosDescriptorLength() == 29 && !device.microsoftOs20Descriptor();
  const auto *bos = device.bosDescriptor();
  ok &= bos && bos[4] == 1;
  ok &= device.end() == ESP_OK;
  {
    EspUsbDevice both; EspUsbDeviceVendor vendor(both);
    ok &= both.begin(config) && both.bosDescriptorLength() == 57 && both.microsoftOs20DescriptorLength() == 168;
    bos = both.bosDescriptor(); ok &= bos && bos[4] == 2;
    ok &= both.end() == ESP_OK;
    config.deviceInterfaceGuid = "invalid";
    ok &= !both.begin(config) && both.lastError() == ESP_ERR_INVALID_ARG;
    ok &= !both.deviceDescriptor() && both.lastError() == ESP_ERR_INVALID_ARG;
  }
  {
    EspUsbDevice sized; SpeedSized function(sized);
    EspUsbDeviceConfig detached; detached.startTinyUsb = false;
    ok &= sized.begin(detached);
    const auto *full = sized.configurationDescriptor(0);
    const auto *high = sized.configurationDescriptorForSpeed(0, true);
    ok &= full && high && full[2] == 18 && high[2] == 27 && full[4] == 1 && high[4] == 1;
    ok &= sized.end() == ESP_OK;
  }
  return ok;
}

EspUsbDevice device;
// A legal vendor Function with opaque class-specific padding. Its own driver
// accounts for that padding before opening the standard bulk endpoints.
class PaddedVendor : public EspUsbDeviceVendor {
public:
  using EspUsbDeviceVendor::EspUsbDeviceVendor;
  uint16_t configurationDescriptor(uint8_t *dst, uint8_t itf, uint8_t ep, uint16_t ceiling) override {
    const uint16_t packet = ceiling > 64 ? 512 : 64;
    interfaceNumber_ = itf;
    if (!dst) return 23 + 768;
    const uint8_t interface[] = {9, 4, itf, 0, 2, 0xff, 0, 0, 0};
    memcpy(dst, interface, 9);
    for (unsigned i = 0; i < 768; i += 96) { memset(dst + 9 + i, 0, 96); dst[9 + i] = 96; dst[10 + i] = 0x24; }
    const uint8_t endpoints[] = {7, 5, ep, 2, uint8_t(packet), uint8_t(packet >> 8), 0, 7, 5, uint8_t(ep | 0x80), 2, uint8_t(packet), uint8_t(packet >> 8), 0};
    memcpy(dst + 9 + 768, endpoints, sizeof(endpoints)); return 23 + 768;
  }
protected:
  uint16_t openBulkInterface(uint8_t port, const uint8_t *d, uint16_t size, uint8_t &in, uint8_t &out, uint16_t &mps) override {
    if (size < 791 || d[5] != 0xff || d[4] != 2) return 0;
    return openBulkPair(port, d + 777, in, out, mps) ? 791 : 0;
  }
};
PaddedVendor *padded = nullptr;
EspUsbDeviceVendor *other[2] = {};
EspUsbDeviceHidCustom *hid[2] = {};
uint8_t longReport[181];
bool started = false;
uint8_t echo[129]; size_t wanted = 0, received = 0;
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); }
  else {
    String command = Serial.readStringUntil('\n'); command.trim();
    if (command == "metadata") {
      unsigned cases = 0; bool ok = writers(cases) && detached() && badSizes();
      const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      for (unsigned i = 0; i < 20 && ok; ++i) ok &= badSizes() && detached();
      const size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      Serial.printf("WRITERS ok=%u cases=%u before=%u after=%u\r\n", unsigned(ok), cases, unsigned(before), unsigned(after));
    } else if (command == "vendor") {
      padded = new PaddedVendor(device);
      for (auto &function : other) function = new EspUsbDeviceVendor(device);
      EspUsbDeviceConfig config; config.product = "v3 descriptor storage"; config.msOs20Layout = ESP_USB_DEVICE_MS_OS_20_FLAT;
      const bool rejected = !device.begin(config) && device.lastError() == ESP_ERR_INVALID_ARG;
      config.msOs20Layout = ESP_USB_DEVICE_MS_OS_20_AUTO;
      started = rejected && device.begin(config);
      const auto *bytes = device.configurationDescriptor(0);
      const unsigned length = bytes ? bytes[2] | unsigned(bytes[3]) << 8 : 0;
      const auto *set = device.microsoftOs20Descriptor();
      unsigned functions = 0, properties = 0, revisions = 0, mask = 0; bool valid = set != nullptr;
      for (size_t i = 10; valid && i < device.microsoftOs20DescriptorLength();) {
        const unsigned n = set[i] | unsigned(set[i + 1]) << 8, type = set[i + 2] | unsigned(set[i + 3]) << 8;
        if (n < 4 || i + n > device.microsoftOs20DescriptorLength()) { valid = false; break; }
        if (type == 2) { ++functions; mask |= 1u << set[i + 4]; }
        if (type == 4) ++properties;
        if (type == 8) { ++revisions; valid &= (set[i + 4] | unsigned(set[i + 5]) << 8) == device.microsoftOs20VendorRevision(); }
        i += n;
      }
      Serial.printf("VENDOR_STARTED ok=%u config=%u ms=%u bos=%u functions=%u properties=%u revisions=%u mask=%u\r\n",
          unsigned(started && valid), length, unsigned(device.microsoftOs20DescriptorLength()), unsigned(device.bosDescriptorLength()), functions, properties, revisions, mask);
    } else if (command.startsWith("expect ")) {
      wanted = command.substring(7).toInt(); received = 0;
      Serial.printf("ARMED %u\r\n", unsigned(wanted));
    } else if (command == "hid") {
      delete padded; padded = nullptr;
      for (auto &function : other) { delete function; function = nullptr; }
      const uint8_t base[] = {0x06,0x00,0xff,0x09,0x01,0xa1,0x01,0x15,0x00,0x26,0xff,0x00,0x75,0x08,0x95,0x01,0x09,0x01,0x81,0x02};
      memcpy(longReport, base, sizeof(base));
      for (unsigned i = 20; i < 180; i += 2) { longReport[i] = 0xa4; longReport[i + 1] = 0xb4; }
      longReport[180] = 0xc0;
      for (auto &function : hid) function = new EspUsbDeviceHidCustom(device, longReport, sizeof(longReport), 1);
      EspUsbDeviceConfig config; config.product = "v3 descriptor storage";
      started = device.begin(config);
      Serial.printf("HID_STARTED ok=%u report=%u bos=%u ms=%u\r\n", unsigned(started), unsigned(device.hidReportDescriptorLength(0)), unsigned(device.bosDescriptorLength()), unsigned(device.microsoftOs20DescriptorLength()));
    } else if (command == "send") {
      bool ok = true;
      for (auto *function : hid) { const uint8_t data = 0x40 + function->compositeReportId(); ok &= function->sendReport(&data, 1); }
      Serial.printf("REPORTS_SENT %u\r\n", unsigned(ok));
    } else if (command == "stop") {
      const auto result = device.end(); started = false; wanted = received = 0;
      Serial.println(result == ESP_OK ? "STOPPED" : "STOP_FAILED");
    }
  }
  if (started && padded && wanted && wanted <= sizeof(echo)) {
    received += padded->read(echo + received, wanted - received);
    if (received == wanted) {
      Serial.printf("ECHOED %u\r\n", unsigned(padded->write(echo, received)));
      wanted = received = 0;
    }
  }
}
