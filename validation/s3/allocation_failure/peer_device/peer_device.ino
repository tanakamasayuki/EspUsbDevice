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
EspUsbDeviceHidVendor *hid[2] = {};
EspUsbDeviceMidi *midi[2] = {};
EspUsbDeviceMsc *msc[2] = {};
EspUsbAudioFunction *audio = nullptr;
EspUsbDeviceDfu *dfu[2] = {};
uint8_t dfuBuffer[130];
alignas(64) uint8_t mscBuffer[512];
EspUsbDeviceMidiPacket midiRx[19], midiTx[21];
EspUsbDeviceHidCustom *inputOnly = nullptr;
const uint8_t pushReport[] = {0x06,0x00,0xff,0x09,0x01,0xa1,0x01,0xa4,0x75,0x08,0x95,0x01,0x09,0x01,0x81,0x02,0xb4,0xc0};
const uint8_t inputReport[] = {0x06,0x00,0xff,0x09,0x01,0xa1,0x01,0x75,0x08,0x95,0x01,0x09,0x01,0x81,0x02,0xc0};
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
      for (unsigned mode = 0; mode < 32 && ok; ++mode) {
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
        if (mode == 6 || mode == 7) {
          config.rxMode = EspUsbBulkRxMode::Disabled; config.rxCapacity = 0;
          points = 5;
          if (mode == 7) { config.txCapacity = config.transferBytes = 0; points = 3; }
        }
        if (mode == 10 || mode == 11) {
          hid[0] = new EspUsbDeviceHidVendor(device, 257); points += 9;
          if (mode == 11) { hid[1] = new EspUsbDeviceHidVendor(device, 63); ++points; }
        }
        if (mode == 12) { inputOnly = new EspUsbDeviceHidCustom(device, inputReport, sizeof(inputReport), 1); points += 7; }
        if (mode == 13) { inputOnly = new EspUsbDeviceHidCustom(device, pushReport, sizeof(pushReport), 1); points += 8; }
        if (mode >= 14 && mode <= 20) {
          midi[0] = new EspUsbDeviceMidi(device);
          EspUsbMidiConfig packets; packets.rxPackets = 1; packets.txPackets = 3; packets.transferPackets = 1;
          unsigned midiPoints = 5;
          if (mode == 15) {
            packets.rxMode = EspUsbMidiRxMode::Callback; packets.rxPackets = 0; midiPoints = 4;
            midi[0]->onPacket([](const EspUsbDeviceMidiPacket &) {});
          }
          if (mode == 16) {
            packets.rxPackets = 17; packets.txPackets = 19;
            packets.rxBuffer = midiRx + 1; packets.txBuffer = midiTx + 1; midiPoints = 3;
          }
          if (mode == 17 || mode == 20) { packets.rxMode = EspUsbMidiRxMode::Disabled; packets.rxPackets = 0; midiPoints -= 2; }
          if (mode == 18 || mode == 20) { packets.txPackets = packets.transferPackets = 0; midiPoints -= 2; }
          ok &= midi[0]->configure(packets) == ESP_OK; points += midiPoints;
          if (mode == 19) { midi[1] = new EspUsbDeviceMidi(device); points += 5; }
        }
        if (mode >= 21 && mode <= 23) {
          msc[0] = new EspUsbDeviceMsc(device);
          msc[0]->onRead([](uint32_t, uint32_t, void *, uint32_t n) { return int32_t(n); });
          ok &= msc[0]->begin(8, 512);
          EspUsbMscConfig storage; storage.transferBytes = 64;
          if (mode == 22) storage.transferBuffer = mscBuffer;
          ok &= msc[0]->configure(storage) == ESP_OK;
          points += mode == 22 ? 3 : 4;
          if (mode == 23) {
            msc[1] = new EspUsbDeviceMsc(device);
            msc[1]->onRead([](uint32_t, uint32_t, void *, uint32_t n) { return int32_t(n); });
            ok &= msc[1]->begin(8, 512); points += 2;
          }
        }
        if (mode >= 24 && mode <= 27) {
          audio = new EspUsbAudioFunction(device, EspUsbAudioProtocol::Uac2);
          if (mode != 26) ok &= audio->addPlaybackStream().addFormat({48000,2,2,16});
          if (mode == 26 || mode == 27) ok &= audio->addCaptureStream().addFormat({48000,2,2,16});
          EspUsbAudioConfig notifications; notifications.eventCapacity = mode == 25 ? 0 : 17;
          ok &= audio->configure(notifications) == ESP_OK;
          points += mode == 27 ? 8 : mode == 26 ? 5 : 6; // State, selected PCM buffers, feedback and class registry.
          if (mode != 25) ++points;
        }
        if (mode >= 28) {
          dfu[0] = new EspUsbDeviceDfu(device, mode == 28 ? EspUsbDeviceDfuMode::Runtime : EspUsbDeviceDfuMode::Download);
          EspUsbDfuConfig firmware; firmware.transferBytes = 129;
          if (mode == 30) firmware.transferBuffer = dfuBuffer + 1;
          ok &= dfu[0]->configure(firmware) == ESP_OK;
          points += mode == 29 || mode == 31 ? 4 : 3;
          if (mode == 31) { dfu[1] = new EspUsbDeviceDfu(device); points += 2; }
        }
        points += 5; // Function index + exact FS, UTF-16 strings, MS OS, and BOS.
        EspUsbDeviceConfig identity;
        if (mode == 8) { identity.webusbEnabled = true; identity.webusbUrl = "https://example.test/日本語"; ++points; }
        if (mode == 9) { identity.manufacturer = identity.product = nullptr; --points; }
        ok &= vendor.configure(config) == ESP_OK;
        for (unsigned point = 1; point <= points && ok; ++point) {
          calls = 0; failAt = point; failureReached = false;
          const bool rejected = !device.begin(identity) && device.lastError() == ESP_ERR_NO_MEM;
          failAt = 0;
          ok &= rejected && failureReached && device.end() == ESP_OK && outstanding == 0 &&
              !vendor.readCapacity() && !vendor.writeCapacity() && !badFree && !overflow;
          if (!ok) { Serial.printf("FAULT_FAILED mode=%u point=%u calls=%u live=%u rejected=%u hit=%u\r\n", mode, point, calls, outstanding, unsigned(rejected), unsigned(failureReached)); break; }
          ++failures;
          calls = 0;
          const bool recovered = device.begin(identity);
          if (!recovered || calls != points) Serial.printf("RECOVERY_FAILED mode=%u point=%u expected=%u calls=%u error=%d\r\n", mode, point, points, calls, int(device.lastError()));
          ok &= recovered && calls == points;
          if (recovered && mode >= 10) {
            const unsigned beforeFeature = calls;
            const uint8_t value = 0x5a;
            for (unsigned update = 0; update < 32 && hid[0]; ++update) ok &= hid[0]->setFeatureReport(&value, 1) == ESP_OK;
            ok &= calls == beforeFeature;
          }
          ok &= vendor.configuration().rxCapacity == config.rxCapacity && vendor.configuration().txCapacity == config.txCapacity;
          ok &= device.end() == ESP_OK && outstanding == 0;
          delay(10);
          if (ok) ++recoveries;
        }
        delete second; second = nullptr;
        delete serialPort; serialPort = nullptr;
        for (auto &function : hid) { delete function; function = nullptr; }
        delete inputOnly; inputOnly = nullptr;
        for (auto &function : midi) { delete function; function = nullptr; }
        for (auto &function : msc) { delete function; function = nullptr; }
        delete audio; audio = nullptr;
        for (auto &function : dfu) { delete function; function = nullptr; }
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
