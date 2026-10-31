#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
#include "esp_heap_caps.h"
#include "../../common/probe_link.h"
#include "../../../../src/internal/EspUsbDwc2Hardware.h"
#include "../../../../src/internal/EspUsbRuntime.h"
// Device side of the public API semantics: additive keyboard state, mouse
// buttons kept across moves, HID packet sizes, the bus speed query, vendor
// requests that share a library vendor code, a bounded end(), TEST_MODE.
EspUsbDevice *device = nullptr;
EspUsbDeviceHidKeyboard *keyboard = nullptr;
EspUsbDeviceHidMouse *mouse = nullptr;
EspUsbDeviceHidCustom *custom = nullptr;
EspUsbDeviceVendor *vendor = nullptr;
std::atomic<unsigned> appRequests{0}, rxEntered{0};
std::atomic<bool> blockRx{false};
// One 8-byte Input report; Report Count 200 makes a report longer than an FS packet.
const uint8_t customDescriptor[] = {
  0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x15, 0x00, 0x26, 0xff, 0x00,
  0x75, 0x08, 0x95, 0xc8, 0x09, 0x01, 0x81, 0x02, 0xc0,
};

const char *speedName() {
  const EspUsbBusSpeed s = device ? device->speed() : EspUsbBusSpeed::None;
  return s == EspUsbBusSpeed::High ? "high" : s == EspUsbBusSpeed::Full ? "full" : "none";
}
void release() {
  if (device) device->end();
  delete keyboard; delete mouse; delete custom; delete vendor; delete device;
  keyboard = nullptr; mouse = nullptr; custom = nullptr; vendor = nullptr; device = nullptr;
}
bool begin(const char *product) {
  EspUsbDeviceConfig config; config.product = product;
  config.webusbEnabled = true; config.webusbUrl = "https://example.com/espusb";
  config.stopTimeoutMs = 300;
  return device->begin(config);
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start keyboard") {
    release();
    device = new EspUsbDevice;
    keyboard = new EspUsbDeviceHidKeyboard(*device);
    vendor = new EspUsbDeviceVendor(*device);
    appRequests = 0; rxEntered = 0; blockRx = false;
    bool ok = vendor->onControlRequest([](const EspUsbDeviceVendorControlRequest &r) {
      if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_SETUP) ++appRequests;
      return vendor->sendControlResponse(r, nullptr, 0);
    }) == ESP_OK;
    ok &= vendor->onRx([](size_t) { ++rxEntered; while (blockRx.load()) delay(1); }) == ESP_OK;
    ok &= begin("v3 API semantics");
    // The observer is read by the worker, so it is set only while stopped.
    ok &= device->onAnyControlRequest(nullptr) == ESP_ERR_INVALID_STATE;
    Serial.printf("STARTED ok=%u\r\n", unsigned(ok));
  } else if (command == "start mouse") {
    release();
    device = new EspUsbDevice; mouse = new EspUsbDeviceHidMouse(*device);
    Serial.printf("STARTED ok=%u\r\n", unsigned(begin("v3 API semantics")));
  } else if (command == "start custom") {
    release();
    device = new EspUsbDevice;
    custom = new EspUsbDeviceHidCustom(*device, customDescriptor, sizeof(customDescriptor), 200);
    Serial.printf("STARTED ok=%u\r\n", unsigned(begin("v3 API semantics")));
  } else if (command == "packet") {
    const unsigned packet = keyboard ? keyboard->inputPacketSize() : mouse ? mouse->inputPacketSize() : custom ? custom->inputPacketSize() : 0;
    Serial.printf("PACKET speed=%s hid=%u\r\n", speedName(), packet);
  } else if (command == "keys") {
    // Each step is one report unless noted; the host checks the bytes in order.
    bool ok = keyboard->pressUsage(4) && keyboard->pressUsage(5) && keyboard->pressUsage(0xe1) && keyboard->pressUsage(6, 0x01);
    ok &= keyboard->releaseUsage(5) && keyboard->releaseUsage(0xe1);
    ok &= keyboard->tapUsage(7);                                  // adds 7, then removes only 7
    ok &= keyboard->pressKey('A') && keyboard->releaseKey('A');   // Shift+4: 4 is already held
    for (uint8_t usage : {4, 5, 7, 8, 9}) ok &= keyboard->pressUsage(usage);
    // A seventh key: refused, no report, state kept.
    const bool full = !keyboard->pressUsage(10) && device->lastError() == ESP_ERR_INVALID_SIZE &&
                      strcmp(device->lastErrorName(), "ESP_ERR_INVALID_SIZE") == 0;
    ok &= keyboard->releaseAll();
    const bool unknown = !keyboard->tapKey('\x01') && device->lastError() == ESP_ERR_NOT_SUPPORTED;
    Serial.printf("KEYS ok=%u full=%u unknown=%u\r\n", unsigned(ok), unsigned(full), unsigned(unknown));
  } else if (command == "mouse") {
    bool ok = mouse->press(1) && mouse->move(5, -3) && mouse->move(1, 1, 0, 0) && mouse->move(0, 0, 0, 2) && mouse->releaseAll();
    const bool invalid = !mouse->move(0, 0, 0, 256) && device->lastError() == ESP_ERR_INVALID_ARG;
    Serial.printf("MOUSE ok=%u invalid=%u\r\n", unsigned(ok), unsigned(invalid));
  } else if (command == "local") {
    // Rejections that happen before anything is sent need no host.
    EspUsbDevice local; EspUsbDeviceHidConsumerControl consumer(local); EspUsbDeviceHidSystemControl system(local);
    bool ok = !consumer.press(0x400) && local.lastError() == ESP_ERR_INVALID_ARG;
    ok &= !system.press(0x84) && local.lastError() == ESP_ERR_INVALID_ARG;
    Serial.printf("LOCAL ok=%u\r\n", unsigned(ok));
  } else if (command == "requests") {
    Serial.printf("REQUESTS app=%u\r\n", appRequests.load());
  } else if (command == "block") {
    blockRx = true; rxEntered = 0;
    Serial.println("BLOCKING");
  } else if (command == "end_blocked") {
    const uint32_t begun = millis();
    while (!rxEntered.load() && millis() - begun < 3000) delay(1);
    const esp_err_t first = device->end();
    blockRx = false;
    delay(20);
    const esp_err_t second = device->end();
    Serial.printf("END entered=%u first=%s second=%s started=%u\r\n", rxEntered.load(), esp_err_to_name(first),
                  esp_err_to_name(second), unsigned(device->started()));
  } else if (command == "test_mode") {
    const auto *r = espusb_dwc2_registers(espusb::internal::usbRuntimeRhport());
    Serial.printf("TEST_MODE selector=%u\r\n", unsigned((r->dctl >> 4) & 7));
  } else if (command == "restart") {
    Serial.println("RESTARTING");
    Serial.flush();
    ESP.restart();
  } else if (command == "stop") {
    release();
    Serial.printf("STOPPED heap=%u\r\n", unsigned(heap_caps_check_integrity_all(true)));
  }
}
