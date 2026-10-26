#include <Arduino.h>
#include <EspUsbDevice.h>
#include "../../../../src/internal/EspUsbMemory.h"
#include "esp_heap_caps.h"

namespace {
struct Allocation { void *pointer; } live[64] = {};
unsigned calls = 0, failAt = 0, outstanding = 0;
bool failureReached = false, badFree = false, overflow = false;
uint8_t externalRx[99], externalTx[133];
}
namespace espusb { namespace internal {
// Strong definitions replace only this project's private allocation boundary.
void *allocateUsbMemory(size_t bytes, size_t alignment, bool dma) {
  ++calls;
  if (failAt && calls == failAt) { failureReached = true; return nullptr; }
  void *memory = alignment ? heap_caps_aligned_alloc(alignment,
      (bytes + alignment - 1) & ~(alignment - 1), MALLOC_CAP_INTERNAL | (dma ? MALLOC_CAP_DMA : 0)) : malloc(bytes);
  if (!memory) return nullptr;
  for (auto &entry : live) if (!entry.pointer) {
    entry.pointer = memory; ++outstanding; return memory;
  }
  overflow = true; free(memory); return nullptr;
}
void freeUsbMemory(void *memory) {
  if (!memory) return;
  for (auto &entry : live) if (entry.pointer == memory) {
    entry.pointer = nullptr; --outstanding; free(memory); return;
  }
  // Includes accidental freeing of externally supplied ring storage.
  badFree = true;
}
} }

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
EspUsbDeviceVendor *second = nullptr;
EspUsbDeviceCdcSerial *serialPort = nullptr;
uint8_t echo[129];
size_t wanted = 0, received = 0;

void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  vendor.onDirectRxComplete([](const EspUsbTransferResult &) {});
  vendor.onRxData([](const uint8_t *, size_t) {});
}
void loop() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n'); command.trim();
    if (command == "faults") {
      bool ok = true;
      unsigned failures = 0, recoveries = 0;
      memset(externalRx, 0x5a, sizeof(externalRx)); memset(externalTx, 0x5a, sizeof(externalTx));
      // Warm descriptor/core/RTOS allocations before checking heap recovery.
      for (unsigned warm = 0; warm < 3 && ok; ++warm) {
        ok = device.begin(); ok &= device.end() == ESP_OK; delay(10);
      }
      const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      for (unsigned mode = 0; mode < 6 && ok; ++mode) {
        EspUsbVendorConfig config;
        unsigned points = 7; // state, two rings, two DMA buffers, two registry tables
        if (mode == 1) { config.rxMode = EspUsbVendorRxMode::Callback; config.rxCapacity = 0; points = 6; }
        if (mode == 2) {
          config.rxCapacity = 97; config.txCapacity = 131; config.transferBytes = 128;
          config.rxBuffer = externalRx + 1; config.txBuffer = externalTx + 1; points = 5;
        }
        if (mode == 3) {
          config.rxMode = EspUsbVendorRxMode::Borrowed;
          config.rxCapacity = config.txCapacity = config.transferBytes = 0; points = 3;
        }
        if (mode == 4) { second = new EspUsbDeviceVendor(device); points = 12; }
        if (mode == 5) { serialPort = new EspUsbDeviceCdcSerial(device); points = 13; }
        ok &= vendor.configure(config) == ESP_OK;
        for (unsigned point = 1; point <= points && ok; ++point) {
          calls = 0; failAt = point; failureReached = false;
          const bool rejected = !device.begin() && device.lastError() == ESP_ERR_NO_MEM;
          failAt = 0;
          ok &= rejected && failureReached && device.end() == ESP_OK && outstanding == 0 &&
              !vendor.readCapacity() && !vendor.writeCapacity() && !badFree && !overflow;
          if (!ok) { Serial.printf("FAULT_FAILED mode=%u point=%u calls=%u live=%u rejected=%u hit=%u\r\n", mode, point, calls, outstanding, unsigned(rejected), unsigned(failureReached)); break; }
          ++failures;
          calls = 0;
          ok &= device.begin() && calls == points;
          ok &= vendor.configuration().rxCapacity == config.rxCapacity && vendor.configuration().txCapacity == config.txCapacity;
          ok &= device.end() == ESP_OK && outstanding == 0;
          delay(10);
          if (ok) ++recoveries;
        }
        delete second; second = nullptr;
        delete serialPort; serialPort = nullptr;
      }
      vendor.configure(EspUsbVendorConfig());
      delay(10);
      const size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      const bool guards = externalRx[0] == 0x5a && externalRx[98] == 0x5a && externalTx[0] == 0x5a && externalTx[132] == 0x5a;
      ok &= guards && !badFree && !overflow && !outstanding && heap_caps_check_integrity_all(true) && before == after;
      Serial.printf("FAULTS ok=%u failed=%u recovered=%u before=%u after=%u\r\n", unsigned(ok), failures, recoveries, unsigned(before), unsigned(after));
    } else if (command == "start") {
      EspUsbDeviceConfig config; config.product = "v3 Vendor API probe";
      Serial.println(device.begin(config) ? "DEVICE_STARTED" : "DEVICE_START_FAILED");
    } else if (command.startsWith("expect ")) {
      wanted = command.substring(7).toInt(); received = 0;
      Serial.printf("ARMED %u\r\n", unsigned(wanted));
    } else if (command == "stop") {
      failAt = 0;
      Serial.println(device.end() == ESP_OK && !outstanding && !badFree ? "STOPPED" : "STOP_FAILED");
      wanted = received = 0;
    }
  }
  if (wanted && wanted <= sizeof(echo)) {
    received += vendor.read(echo + received, wanted - received);
    if (received == wanted) {
      if (vendor.write(echo, received) == received) Serial.printf("ECHOED %u\r\n", unsigned(received));
      else Serial.println("ECHO_FAILED");
      wanted = received = 0;
    }
  }
  delay(1);
}
