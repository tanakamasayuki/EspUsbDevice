#include <Arduino.h>
#include <EspUsbDevice.h>
#include "esp_heap_caps.h"
#include <atomic>

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
EspUsbDeviceVendor *second = nullptr;
bool started = false;
uint8_t buffer[129];
size_t expected = 0;
size_t received = 0;
size_t chunk = sizeof(buffer);
bool explicitFlush = false;
bool directMode = false, terminate = false, echoPending = false;
alignas(64) uint8_t directBuffer[192];
EspUsbTransferResult directResult;
std::atomic<unsigned> completions{0};
std::atomic<unsigned> rejectedWorkerEnds{0};
unsigned echoCompletion = 0;
bool callbackReceive = false;
std::atomic<size_t> callbackRx{0}, callbackTx{0};
uint8_t externalRx[99], externalTx[133];

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);
  vendor.onDirectComplete([](const EspUsbTransferResult &result) {
    // Deliberate invalid re-entry: reject it without ending the active worker.
    if (device.end() == ESP_ERR_INVALID_STATE) rejectedWorkerEnds.fetch_add(1);
    directResult = result;
    completions.fetch_add(1, std::memory_order_release);
  });
}

void loop() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command == "start") {
      EspUsbDeviceConfig config;
      config.product = "v3 Vendor API probe";
      started = device.begin(config);
      Serial.println(started ? "DEVICE_STARTED" : "DEVICE_START_FAILED");
    } else if (command == "batch_probe") {
      vendor.flush(); // An empty flush must not arm future writes.
      const uint8_t byte = 1;
      const bool staged = vendor.write(&byte, 1) == 1 && vendor.writeAvailable() + 1 == vendor.writeCapacity();
      Serial.printf("BATCH_STAGED %u\r\n", unsigned(staged));
    } else if (command == "flush") {
      vendor.flush();
      Serial.println("FLUSHED");
    } else if (command == "callback_mutation") {
      const bool rejected = vendor.onRx(nullptr) == ESP_ERR_INVALID_STATE &&
          vendor.onRxData(nullptr) == ESP_ERR_INVALID_STATE &&
          vendor.onTxComplete(nullptr) == ESP_ERR_INVALID_STATE &&
          vendor.onDirectComplete(nullptr) == ESP_ERR_INVALID_STATE &&
          vendor.onControlRequest(nullptr) == ESP_ERR_INVALID_STATE;
      Serial.printf("CALLBACK_MUTATION_REJECTED %u\r\n", unsigned(rejected));
    } else if (command == "start_again") {
      const bool rejected = !device.begin() && device.lastError() == ESP_ERR_INVALID_STATE;
      Serial.printf("START_AGAIN_REJECTED %u\r\n", unsigned(rejected));
    } else if (command == "external") {
      EspUsbVendorConfig config;
      config.rxCapacity = 97; config.txCapacity = 131; config.transferBytes = 128;
      config.rxBuffer = externalRx + 1; config.txBuffer = externalTx + 1;
      memset(externalRx, 0x5a, sizeof(externalRx)); memset(externalTx, 0x5a, sizeof(externalTx));
      const bool configured = vendor.configure(config) == ESP_OK;
      auto overlap = config;
      overlap.txBuffer = config.rxBuffer;
      const bool rejected = vendor.configure(overlap) == ESP_ERR_INVALID_ARG;
      Serial.printf("EXTERNAL_CONFIGURED %u\r\n", unsigned(configured && rejected));
    } else if (command == "canaries") {
      Serial.printf("CANARIES %u\r\n", unsigned(externalRx[0] == 0x5a && externalRx[98] == 0x5a &&
                    externalTx[0] == 0x5a && externalTx[132] == 0x5a));
    } else if (command.startsWith("configure ")) {
      unsigned rx, tx, transfer, automatic;
      if (sscanf(command.c_str(), "configure %u %u %u %u", &rx, &tx, &transfer, &automatic) != 4 || transfer > UINT16_MAX) {
        Serial.println("INVALID_CONFIG_COMMAND");
      } else {
        EspUsbVendorConfig config;
        config.rxCapacity = rx; config.txCapacity = tx;
        config.transferBytes = transfer; config.autoFlush = automatic;
        const esp_err_t result = vendor.configure(config);
        Serial.printf("CONFIG_RESULT %d rx=%u tx=%u\r\n", int(result),
                      unsigned(vendor.configuration().rxCapacity), unsigned(vendor.configuration().txCapacity));
      }
    } else if (command == "second") {
      if (!started && !second) {
        second = new EspUsbDeviceVendor(device);
        EspUsbVendorConfig config;
        config.rxCapacity = 512; config.txCapacity = 256; config.transferBytes = 128;
        if (second->configure(config) == ESP_OK) {
          directMode = false;
          Serial.println("SECOND_REGISTERED");
        }
      }
    } else if (command == "second_callback") {
      auto config = second->configuration();
      config.rxCapacity = 0;
      config.rxMode = EspUsbVendorRxMode::Callback;
      second->onRxData([](const uint8_t *data, size_t n) {
        // read() is inactive in callback mode and must not re-arm this buffer.
        uint8_t ignored;
        if (second->read(&ignored, 1) != 0) return;
        callbackTx.fetch_add(second->write(data, n));
        callbackRx.fetch_add(n); // Publish only after the echo was accepted.
      });
      if (!started && second->configure(config) == ESP_OK) {
        callbackReceive = true;
        Serial.println("CALLBACK_CONFIGURED");
      }
    } else if (command == "second_capacities") {
      Serial.printf("SECOND_CAPACITIES %u %u %u %u\r\n", unsigned(vendor.readCapacity()),
                    unsigned(vendor.writeCapacity()), unsigned(second->readCapacity()), unsigned(second->writeCapacity()));
    } else if (command == "direct_mode") {
      directMode = true;
      Serial.println("DIRECT_MODE");
    } else if (command == "hold") {
      memset(directBuffer, 0x5a, sizeof(directBuffer));
      const bool accepted = vendor.writeDirect(directBuffer, 129);
      const bool busy = !vendor.writeDirect(directBuffer, 1) && vendor.lastDirectError() == EspUsbDeviceVendorDirectError::Busy;
      Serial.printf("HELD %u busy=%u\r\n", unsigned(accepted), unsigned(busy));
    } else if (command == "end_status") {
      Serial.printf("END_STATUS %u error=%d\r\n", rejectedWorkerEnds.load(), int(device.lastError()));
    } else if (command == "direct_status") {
      const unsigned count = completions.load(std::memory_order_acquire);
      Serial.printf("DIRECT_STATUS %u status=%u requested=%u actual=%u known=%u pointer=%u\r\n",
                    count, unsigned(directResult.status), unsigned(directResult.requestedBytes),
                    unsigned(directResult.actualBytes), unsigned(directResult.actualBytesKnown),
                    unsigned(directResult.buffer == directBuffer));
    } else if (command == "direct_invalid") {
      const unsigned before = completions.load();
      const bool nullRejected = !vendor.writeDirect(nullptr, 1);
      const bool alignmentRejected = !vendor.writeDirect(directBuffer + 1, 1) && vendor.lastDirectError() == EspUsbDeviceVendorDirectError::NotAligned;
      const bool lengthRejected = !vendor.writeDirect(directBuffer, 65536);
      const bool packetsRejected = !vendor.writeDirect(directBuffer, 65535) && vendor.lastDirectError() == EspUsbDeviceVendorDirectError::BadArgument;
      Serial.printf("DIRECT_INVALID %u max=%u\r\n", unsigned(nullRejected && alignmentRejected && lengthRejected && packetsRejected && completions.load() == before), unsigned(vendor.maxDirectTransfer()));
    } else if (command == "capacities") {
      Serial.printf("CAPACITIES %u %u\r\n", unsigned(vendor.readCapacity()), unsigned(vendor.writeCapacity()));
    } else if (command == "allocation_failure") {
      const auto saved = vendor.configuration();
      auto excessive = saved;
      excessive.rxCapacity = 16 * 1024 * 1024;
      excessive.rxBuffer = nullptr;
      vendor.configure(excessive);
      const bool rejected = !device.begin() && device.lastError() == ESP_ERR_NO_MEM;
      device.end();
      const bool restored = vendor.configure(saved) == ESP_OK;
      Serial.println(rejected && restored && !vendor.readCapacity() ? "ALLOCATION_ROLLBACK_OK" : "ALLOCATION_ROLLBACK_FAILED");
    } else if (command == "cycles") {
      // Warm lazy RTOS/core allocations before comparing resource recovery.
      bool ok = !started;
      for (unsigned i = 0; i < 3 && ok; ++i) {
        ok = device.begin(); device.end(); delay(10);
      }
      const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      for (unsigned i = 0; i < 100 && ok; ++i) {
        ok = device.begin(); device.end(); delay(10);
        ok = ok && !vendor.readCapacity() && !vendor.writeCapacity();
      }
      const size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      Serial.printf("CYCLES %u before=%u after=%u\r\n", unsigned(ok), unsigned(before), unsigned(after));
    } else if (command == "status") {
      Serial.printf("DEVICE_READY %u\r\n", unsigned(device.ready()));
    } else if (command.startsWith("expect ")) {
      unsigned length = 0, requestedChunk = 0, flush = 0, end = 0;
      sscanf(command.c_str(), "expect %u %u %u %u", &length, &requestedChunk, &flush, &end);
      if (length == 0 || length > sizeof(buffer) || expected) {
        Serial.println("INVALID_EXPECT");
      } else {
        expected = length;
        received = 0;
        callbackRx.store(0); callbackTx.store(0);
        chunk = requestedChunk ? requestedChunk : sizeof(buffer);
        explicitFlush = flush != 0;
        terminate = end != 0;
        Serial.printf("ARMED %u\r\n", unsigned(expected));
      }
    } else if (command == "stop") {
      if (device.end() != ESP_OK) { Serial.println("STOP_FAILED"); return; }
      started = false;
      expected = received = 0;
      Serial.println("STOPPED");
    }
  }
  auto &channel = second ? *second : vendor;
  if (started && expected && callbackReceive && callbackRx.load() >= expected) {
    if (callbackRx.load() == expected && callbackTx.load() == expected)
      Serial.printf("ECHOED %u\r\n", unsigned(expected));
    else Serial.println("CALLBACK_ECHO_FAILED");
    expected = received = 0;
  }
  if (started && expected && !callbackReceive) {
    received += channel.read(buffer + received, expected - received);
    if (received == expected) {
      if (directMode) {
        memcpy(directBuffer, buffer, received);
        echoCompletion = completions.load(std::memory_order_acquire) + 1;
        echoPending = true; // Prepare bookkeeping before another core can complete.
        if (!vendor.writeDirect(directBuffer, received,
            terminate ? EspUsbTransferTermination::EndWithShortPacket : EspUsbTransferTermination::Continue)) {
          echoPending = false;
          Serial.println("DIRECT_REJECTED");
        }
        expected = received = 0;
      } else {
      size_t sent = 0;
      const uint32_t deadline = millis() + 3000;
      while (sent < received && int32_t(deadline - millis()) > 0) {
        const size_t remaining = received - sent;
        sent += channel.write(buffer + sent, remaining < chunk ? remaining : chunk);
        if (explicitFlush) channel.flush();
        delay(1);
      }
      if (sent == received) Serial.printf("ECHOED %u\r\n", unsigned(sent));
      else Serial.println("ECHO_FAILED");
      expected = received = 0;
      }
    }
  }
  if (echoPending && completions.load(std::memory_order_acquire) >= echoCompletion) {
    echoPending = false;
    if (directResult.status == EspUsbTransferStatus::Success && directResult.actualBytesKnown &&
        directResult.requestedBytes == directResult.actualBytes && directResult.buffer == directBuffer)
      Serial.printf("ECHOED %u\r\n", unsigned(directResult.actualBytes));
    else Serial.println("DIRECT_COMPLETION_FAILED");
  }
  delay(1);
}
