#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
#include "esp_heap_caps.h"

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
uint8_t *storage = nullptr;
size_t limit = 0, allocation = 0, requested = 0, expected = 0;
uint64_t submittedId = 0;
EspUsbTransferResult returned;
EspUsbTransferStatus expectedStatus = EspUsbTransferStatus::Success;
std::atomic<unsigned> completions{0};
unsigned target = 0;
uint8_t patternByte(size_t offset, size_t seed) {
  return uint8_t(offset * 37 + (offset >> 8) * 17 + (offset >> 16) * 53 + seed);
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);
  EspUsbVendorConfig config;
  config.rxMode = EspUsbVendorRxMode::Borrowed;
  config.rxCapacity = config.txCapacity = config.transferBytes = 0;
  vendor.configure(config);
  const auto complete = [](const EspUsbTransferResult &result) {
    returned = result;
    completions.fetch_add(1, std::memory_order_release);
  };
  vendor.onDirectTxComplete(complete);
  vendor.onDirectRxComplete(complete);
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    EspUsbDeviceConfig config;
    config.product = "v3 transfer limits";
    Serial.println(device.begin(config) ? "DEVICE_STARTED" : "DEVICE_START_FAILED");
  } else if (command == "limits") {
    const size_t txLimit = vendor.maxDirectTransfer();
    limit = 131072;
    const size_t rxLimit = vendor.maxDirectReceive();
    allocation = ((limit + 63) & ~size_t(63)) + 128;
    storage = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, allocation, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    Serial.printf("LIMITS tx=%u rx=%u capacity=%u allocated=%u\r\n", unsigned(txLimit), unsigned(rxLimit), unsigned(limit), unsigned(storage != nullptr));
  } else if (command.startsWith("tx ") || command.startsWith("rx ")) {
    unsigned length = 0, amount = 0;
    const bool tx = command.startsWith("tx ");
    sscanf(command.c_str() + 3, "%u %u", &length, &amount);
    if (!storage || !length || length > limit || (!tx && amount > length)) { Serial.println("INVALID_TRANSFER"); return; }
    requested = length; expected = tx ? length : amount;
    expectedStatus = EspUsbTransferStatus::Success;
    memset(storage, 0xa5, allocation);
    if (tx) for (size_t i = 0; i < length; ++i) storage[64 + i] = patternByte(i, length);
    target = completions.load(std::memory_order_acquire) + 1;
    const auto result = tx ? vendor.writeDirect(storage + 64, length,
        amount ? EspUsbTransferTermination::EndWithShortPacket : EspUsbTransferTermination::Continue)
        : vendor.readDirect(storage + 64, length);
    submittedId = result.id;
    Serial.printf("POSTED %u error=%u\r\n", unsigned(bool(result)), unsigned(result.error));
  } else if (command == "cancel tx" || command == "cancel rx") {
    expectedStatus = EspUsbTransferStatus::Cancelled;
    const auto result = command == "cancel tx" ? vendor.cancelDirectTx(submittedId) : vendor.cancelDirectRx(submittedId);
    Serial.printf("CANCELLED_REQUEST %d\r\n", int(result));
  } else if (command == "expect_reset") {
    expectedStatus = EspUsbTransferStatus::BusReset;
    Serial.println("RESET_EXPECTED");
  } else if (command == "end") {
    expectedStatus = EspUsbTransferStatus::Stopped;
    const auto error = device.end();
    Serial.printf("ENDED %u\r\n", unsigned(error == ESP_OK && !device.started()));
  } else if (command == "result") {
    const uint32_t deadline = millis() + 5000;
    while (completions.load(std::memory_order_acquire) < target && int32_t(deadline - millis()) > 0) delay(1);
    bool guards = true, payload = true;
    for (size_t i = 0; i < 64; ++i) guards &= storage[i] == 0xa5;
    for (size_t i = 64 + requested; i < allocation; ++i) guards &= storage[i] == 0xa5;
    if (expectedStatus == EspUsbTransferStatus::Success)
      for (size_t i = 0; i < expected; ++i) payload &= storage[64 + i] == patternByte(i, expected);
    const bool result = completions.load(std::memory_order_acquire) == target &&
        returned.status == expectedStatus &&
        returned.actualBytesKnown == (expectedStatus == EspUsbTransferStatus::Success) &&
        returned.actualBytes == (expectedStatus == EspUsbTransferStatus::Success ? expected : 0) && returned.requestedBytes == requested &&
        returned.buffer == storage + 64 && submittedId && returned.id == submittedId;
    Serial.printf("RESULT count=%u ok=%u guards=%u payload=%u\r\n", completions.load(), unsigned(result), unsigned(guards), unsigned(payload));
  } else if (command == "stop") {
    const esp_err_t result = device.end();
    if (result != ESP_OK) { Serial.println("STOP_FAILED"); return; }
    heap_caps_free(storage); storage = nullptr;
    Serial.println("STOPPED");
  }
}
