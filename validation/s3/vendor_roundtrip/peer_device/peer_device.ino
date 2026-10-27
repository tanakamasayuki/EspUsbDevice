#include <Arduino.h>
#include <EspUsbDevice.h>
#include "esp_heap_caps.h"
#include <atomic>
#if defined(ESPUSB_CANCEL_FENCE_FAULT) || defined(ESPUSB_CANCEL_LIFECYCLE_FAULT)
#include "../../../../src/device/usbd.h"
#include "../../../../src/internal/EspUsbDeviceEndpoint.h"
#include "../../../../src/internal/EspUsbController.h"
#include "../../../../src/internal/EspUsbTinyUsbRuntime.h"
#endif

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
alignas(64) uint8_t postedStorage[384];
EspUsbTransferResult rxResult;
std::atomic<unsigned> rxCompletions{0};
unsigned rxTarget = 0;
size_t rxExpectedBytes = 0, rxCapacity = 0;
bool rxPending = false;
alignas(64) uint8_t chainStorage[192];
std::atomic<bool> chainRequested{false}, chainSubmitted{false}, chainFirstValid{false};
std::atomic<bool> chainAfterCancel{false};
unsigned chainTarget = 0;
uint64_t lastTxId = 0, lastRxId = 0, staleTxId = 0, staleRxId = 0;

#ifdef ESPUSB_CANCEL_FENCE_FAULT
std::atomic<bool> faultDone{false};
std::atomic<unsigned> faultEvents{0};
std::atomic<int> faultTx{-1}, faultRx{-1};
std::atomic<bool> faultSaturated{false};
void faultNoop(void *) {}
void fillFenceQueue(void *) {
  // Force the first fence enqueue to fail without blocking the USB worker.
  while (usbd_try_defer_func(faultNoop, nullptr)) {}
  faultSaturated.store(true);
}
espusb::internal::TinyUsbRuntimeJob faultFillJob{fillFenceQueue, nullptr};
void injectCancelFence(void *) {
  // Queue old completions on the worker before it can execute the abort job.
  if (tud_task_event_ready() || usbd_event_queue_capacity() < 4) { faultDone.store(true); return; }
  const uint8_t *descriptor = device.configurationDescriptor(0);
  const unsigned length = descriptor[2] | (unsigned(descriptor[3]) << 8);
  uint8_t in = 0, out = 0;
  for (unsigned i = 0; i + 2 <= length && descriptor[i] >= 2; i += descriptor[i]) {
    if (i + descriptor[i] > length) break;
    if (descriptor[i + 1] == TUSB_DESC_ENDPOINT && descriptor[i] >= 7) {
      if (descriptor[i + 2] & 0x80) in = descriptor[i + 2];
      else out = descriptor[i + 2];
    }
  }
  if (!in || !out) { faultDone.store(true); return; }
  espusb::internal::postTinyUsbRuntimeJob(faultFillJob);
  faultTx.store(vendor.cancelDirectTx()); faultRx.store(vendor.cancelDirectRx());
  if (faultTx.load() == ESP_OK && faultRx.load() == ESP_OK) {
    // This callback consumes one event of the per-run budget, leaving at
    // least one old completion queued when the worker first services abort.
    const auto port = espusb::internal::tinyUsbRuntimeRhport();
    for (unsigned i = 0; i < usbd_event_queue_capacity(); ++i)
      espusb_controller_event_xfer_complete(port, i & 1 ? out : in, 0, XFER_RESULT_SUCCESS, false);
    faultEvents.store(usbd_event_queue_capacity());
  }
  faultDone.store(true);
}
#endif

#ifdef ESPUSB_CANCEL_LIFECYCLE_FAULT
// Park only the USB worker, never its ISR or a lock. The sketch and Host can
// arrange a real bus reset or cooperative end while cancellation is pending.
std::atomic<bool> lifecycleParked{false}, lifecycleRelease{false}, lifecycleTimedOut{false};
std::atomic<unsigned> lifecycleResets{0};
unsigned lifecycleResetMark = 0;
std::atomic<bool> lifecycleEndDone{false};
std::atomic<int> lifecycleEndResult{-1}, lifecycleTx{-1}, lifecycleRx{-1};
extern "C" void tud_event_hook_cb(uint8_t, uint32_t event, bool) {
  if (event == ESPUSB_CONTROLLER_EVENT_BUS_RESET) lifecycleResets.fetch_add(1);
}
void parkLifecycle(void *) {
  lifecycleParked.store(true);
  const uint32_t deadline = millis() + 12000;
  while (!lifecycleRelease.load() && int32_t(deadline - millis()) > 0) delay(1);
  lifecycleTimedOut.store(!lifecycleRelease.load());
  lifecycleParked.store(false);
}
espusb::internal::TinyUsbRuntimeJob lifecycleParkJob{parkLifecycle, nullptr};
void cancelThenPark(void *) {
  lifecycleTx.store(vendor.cancelDirectTx(lastTxId));
  lifecycleRx.store(vendor.cancelDirectRx(lastRxId));
  // FIFO jobs: abort runs first and queues its fences, then this job parks the
  // worker before it can process those fences. end must safely discard them.
  espusb::internal::postTinyUsbRuntimeJob(lifecycleParkJob);
}
void endLifecycle(void *) {
  lifecycleEndResult.store(device.end());
  lifecycleEndDone.store(true);
  vTaskDelete(nullptr);
}
#endif

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);
  vendor.onDirectTxComplete([](const EspUsbTransferResult &result) {
    // Deliberate invalid re-entry: reject it without ending the active worker.
    if (device.end() == ESP_ERR_INVALID_STATE) rejectedWorkerEnds.fetch_add(1);
    directResult = result;
    completions.fetch_add(1, std::memory_order_release);
  });
  vendor.onDirectRxComplete([](const EspUsbTransferResult &result) {
    rxResult = result;
    rxCompletions.fetch_add(1, std::memory_order_release);
    if (chainRequested.exchange(false)) {
      const bool expected = chainAfterCancel.load()
          ? result.status == EspUsbTransferStatus::Cancelled && !result.actualBytesKnown
          : result.status == EspUsbTransferStatus::Success && result.actualBytesKnown && result.actualBytes == 1 && postedStorage[64] == 1;
      chainFirstValid.store(expected && result.buffer == postedStorage + 64);
      chainSubmitted.store(bool(vendor.readDirect(chainStorage + 64, 64)));
    }
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
    } else if (command == "rx_mode") {
      if (!started) {
        delete second; second = nullptr;
        EspUsbVendorConfig config;
        config.rxMode = EspUsbVendorRxMode::Borrowed;
        config.rxCapacity = config.txCapacity = 0;
        config.transferBytes = 0; // No copied transfers in either direction.
        if (vendor.configure(config) == ESP_OK) {
          callbackReceive = false;
          Serial.println("RX_MODE");
        }
      }
    } else if (command.startsWith("rx_arm ")) {
      unsigned capacity = 0, bytes = 0;
      sscanf(command.c_str(), "rx_arm %u %u", &capacity, &bytes);
      if (rxPending || capacity > 256 || bytes > capacity) {
        Serial.println("INVALID_RX_COMMAND");
      } else {
        memset(postedStorage, 0xa5, sizeof(postedStorage));
        rxExpectedBytes = bytes; rxCapacity = capacity;
        rxTarget = rxCompletions.load(std::memory_order_acquire) + 1;
        rxPending = true;
        const auto result = vendor.readDirect(postedStorage + 64, capacity);
        lastRxId = result.id;
        if (!result) rxPending = false;
        Serial.printf("RX_ARMED %u error=%u\r\n", unsigned(bool(result)), unsigned(result.error));
      }
    } else if (command == "rx_chain") {
      memset(postedStorage, 0xa5, sizeof(postedStorage));
      memset(chainStorage, 0xa5, sizeof(chainStorage));
      chainTarget = rxCompletions.load(std::memory_order_acquire) + 2;
      chainFirstValid.store(false); chainSubmitted.store(false); chainRequested.store(true);
      chainAfterCancel.store(false);
      const auto result = vendor.readDirect(postedStorage + 64, 64);
      Serial.printf("RX_CHAIN_ARMED %u\r\n", unsigned(bool(result)));
    } else if (command == "rx_chain_result") {
      const uint32_t deadline = millis() + 3000;
      while (rxCompletions.load(std::memory_order_acquire) < chainTarget && int32_t(deadline - millis()) > 0) delay(1);
      bool guards = true, payload = true;
      for (size_t i = 0; i < 64; ++i) guards &= postedStorage[i] == 0xa5 && chainStorage[i] == 0xa5;
      for (size_t i = 128; i < sizeof(postedStorage); ++i) guards &= postedStorage[i] == 0xa5;
      for (size_t i = 128; i < sizeof(chainStorage); ++i) guards &= chainStorage[i] == 0xa5;
      for (size_t i = 0; i < 63; ++i) payload &= chainStorage[64 + i] == uint8_t(i * 37 + 63);
      const bool result = rxCompletions.load(std::memory_order_acquire) == chainTarget &&
          chainFirstValid.load() && chainSubmitted.load() && guards && payload &&
          rxResult.status == EspUsbTransferStatus::Success && rxResult.actualBytesKnown &&
          rxResult.buffer == chainStorage + 64 && rxResult.actualBytes == 63;
      Serial.printf("RX_CHAIN_RESULT %u\r\n", unsigned(result));
    } else if (command == "cancel_both") {
      staleTxId = lastTxId; staleRxId = lastRxId;
      const auto tx = vendor.cancelDirectTx(staleTxId);
      const auto rx = vendor.cancelDirectRx(staleRxId);
      Serial.printf("CANCEL_REQUEST tx=%d rx=%d\r\n", int(tx), int(rx));
    } else if (command == "cancel_stale") {
      Serial.printf("CANCEL_STALE tx=%d rx=%d\r\n", int(vendor.cancelDirectTx(staleTxId)), int(vendor.cancelDirectRx(staleRxId)));
    } else if (command == "cancel_empty") {
      Serial.printf("CANCEL_EMPTY tx=%d rx=%d\r\n", int(vendor.cancelDirectTx()), int(vendor.cancelDirectRx()));
    } else if (command.startsWith("tx_wait ")) {
      const unsigned target = command.substring(8).toInt();
      const uint32_t deadline = millis() + 3000;
      while (completions.load(std::memory_order_acquire) < target && int32_t(deadline - millis()) > 0) delay(1);
      Serial.printf("TX_WAIT %u\r\n", completions.load(std::memory_order_acquire));
    } else if (command.startsWith("tx_post ")) {
      const unsigned n = command.substring(8).toInt();
      if (!n || n > sizeof(directBuffer)) { Serial.println("INVALID_TX_COMMAND"); return; }
      for (size_t i = 0; i < n; ++i) directBuffer[i] = uint8_t(i * 37 + n);
      const auto submission = vendor.writeDirect(directBuffer, n);
      lastTxId = submission.id;
      Serial.printf("TX_POSTED %u error=%u\r\n", unsigned(bool(submission)), unsigned(submission.error));
    } else if (command == "rx_busy") {
      const auto busy = vendor.readDirect(postedStorage + 64, 64);
      Serial.printf("RX_BUSY %u\r\n", unsigned(!busy && busy.error == EspUsbTransferError::Busy));
    } else if (command == "rx_count") {
      Serial.printf("RX_COUNT %u\r\n", rxCompletions.load(std::memory_order_acquire));
    } else if (command == "rx_invalid") {
      const auto zero = vendor.readDirect(postedStorage + 64, 0);
      const auto length = vendor.readDirect(postedStorage + 64, 63);
      const auto alignment = vendor.readDirect(postedStorage + 65, 64);
      const bool ok = !zero && zero.error == EspUsbTransferError::BadArgument &&
          !length && length.error == EspUsbTransferError::BadArgument &&
          !alignment && alignment.error == EspUsbTransferError::NotAligned;
      Serial.printf("RX_INVALID %u max=%u\r\n", unsigned(ok), unsigned(vendor.maxDirectReceive()));
    } else if (command == "rx_result") {
      const uint32_t deadline = millis() + 3000;
      while (rxCompletions.load(std::memory_order_acquire) < rxTarget && int32_t(deadline - millis()) > 0) delay(1);
      bool guards = true, payload = true;
      for (size_t i = 0; i < 64; ++i) guards &= postedStorage[i] == 0xa5;
      for (size_t i = 64 + rxCapacity; i < sizeof(postedStorage); ++i) guards &= postedStorage[i] == 0xa5;
      if (rxResult.status == EspUsbTransferStatus::Success && rxResult.actualBytesKnown) {
        payload = rxResult.actualBytes == rxExpectedBytes;
        for (size_t i = 0; i < rxResult.actualBytes && i < rxCapacity; ++i)
          payload &= postedStorage[64 + i] == uint8_t(i * 37 + rxExpectedBytes);
      }
      Serial.printf("RX_RESULT %u status=%u requested=%u actual=%u known=%u pointer=%u guards=%u payload=%u\r\n",
          rxCompletions.load(std::memory_order_acquire), unsigned(rxResult.status),
          unsigned(rxResult.requestedBytes), unsigned(rxResult.actualBytes), unsigned(rxResult.actualBytesKnown),
          unsigned(rxResult.buffer == postedStorage + 64), unsigned(guards), unsigned(payload));
      if (rxCompletions.load(std::memory_order_acquire) >= rxTarget) rxPending = false;
#ifdef ESPUSB_CANCEL_FENCE_FAULT
    } else if (command == "fault_cancel") {
      memset(chainStorage, 0xa5, sizeof(chainStorage));
      chainTarget = rxCompletions.load(std::memory_order_acquire) + 2;
      chainFirstValid.store(false); chainSubmitted.store(false);
      chainAfterCancel.store(true); chainRequested.store(true);
      staleTxId = lastTxId; staleRxId = lastRxId;
      faultDone.store(false); faultEvents.store(0); faultTx.store(-1); faultRx.store(-1); faultSaturated.store(false);
      Serial.printf("FAULT_REQUESTED %u\r\n", unsigned(usbd_try_defer_func(injectCancelFence, nullptr)));
    } else if (command == "fault_status") {
      const uint32_t deadline = millis() + 3000;
      while ((!faultDone.load() || !faultSaturated.load()) && int32_t(deadline - millis()) > 0) delay(1);
      Serial.printf("FAULT_STATUS events=%u full=%u tx=%d rx=%d\r\n", faultEvents.load(), unsigned(faultSaturated.load()), faultTx.load(), faultRx.load());
#endif
#ifdef ESPUSB_CANCEL_LIFECYCLE_FAULT
    } else if (command == "fault_park" || command == "fault_park_cancelled") {
      lifecycleRelease.store(false); lifecycleParked.store(false); lifecycleTimedOut.store(false);
      lifecycleResetMark = lifecycleResets.load();
      staleTxId = lastTxId; staleRxId = lastRxId;
      const bool queued = usbd_try_defer_func(command == "fault_park" ? parkLifecycle : cancelThenPark, nullptr);
      const uint32_t deadline = millis() + 3000;
      while (queued && !lifecycleParked.load() && int32_t(deadline - millis()) > 0) delay(1);
      Serial.printf("LIFECYCLE_PARKED %u\r\n", unsigned(queued && lifecycleParked.load()));
    } else if (command == "fault_reset_release") {
      const uint32_t deadline = millis() + 5000;
      while (lifecycleResets.load() == lifecycleResetMark && int32_t(deadline - millis()) > 0) delay(1);
      const bool reset = lifecycleResets.load() != lifecycleResetMark;
      const bool parked = lifecycleParked.load() && !lifecycleTimedOut.load();
      lifecycleRelease.store(true);
      Serial.printf("LIFECYCLE_RESET observed=%u parked=%u\r\n", unsigned(reset), unsigned(parked));
    } else if (command == "fault_end_release") {
      lifecycleEndDone.store(false); lifecycleEndResult.store(-1);
      const bool parked = lifecycleParked.load() && !lifecycleTimedOut.load();
      TaskHandle_t task = nullptr;
      const bool created = xTaskCreate(endLifecycle, "probe-end", 4096, nullptr, 5, &task) == pdPASS;
      bool stopping = false;
      const uint32_t deadline = millis() + 5000;
      while (created && !stopping && int32_t(deadline - millis()) > 0) {
        stopping = vendor.cancelDirectTx(lastTxId) == ESP_ERR_INVALID_STATE;
        if (!stopping) delay(1);
      }
      lifecycleRelease.store(true);
      while (created && !lifecycleEndDone.load() && int32_t(deadline - millis()) > 0) delay(1);
      const bool done = created && lifecycleEndDone.load();
      if (done && lifecycleEndResult.load() == ESP_OK) started = false;
      Serial.printf("LIFECYCLE_END parked=%u stopping=%u done=%u error=%d\r\n",
          unsigned(parked), unsigned(stopping), unsigned(done), lifecycleEndResult.load());
#endif
    } else if (command == "copy_terminated") {
      EspUsbBulkStreamConfig config;
      config.rxCapacity = 64; config.txCapacity = 256; config.transferBytes = 128;
      config.streamTermination = EspUsbStreamTermination::EndWithShortPacket;
      const auto result = vendor.configure(config);
      auto invalid = config; invalid.streamTermination = static_cast<EspUsbStreamTermination>(255);
      const bool kept = vendor.configure(invalid) == ESP_ERR_INVALID_ARG &&
          vendor.configuration().streamTermination == config.streamTermination;
      Serial.printf("COPY_TERMINATED %u\r\n", unsigned(result == ESP_OK && kept));
    } else if (command.startsWith("copy_post ")) {
      const unsigned n = command.substring(10).toInt();
      if (!n || n > sizeof(directBuffer)) { Serial.println("INVALID_TX_COMMAND"); return; }
      for (size_t i = 0; i < n; ++i) directBuffer[i] = uint8_t(i * 37 + n);
      Serial.printf("COPY_POSTED %u\r\n", unsigned(vendor.write(directBuffer, n)));
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
          vendor.onDirectTxComplete(nullptr) == ESP_ERR_INVALID_STATE &&
          vendor.onDirectRxComplete(nullptr) == ESP_ERR_INVALID_STATE &&
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
      const auto accepted = vendor.writeDirect(directBuffer, 129);
      lastTxId = accepted.id;
      const auto duplicate = vendor.writeDirect(directBuffer, 1);
      const bool busy = !duplicate && duplicate.error == EspUsbTransferError::Busy && duplicate.id == 0;
      Serial.printf("HELD %u busy=%u\r\n", unsigned(bool(accepted)), unsigned(busy));
    } else if (command == "end_status") {
      Serial.printf("END_STATUS %u error=%d\r\n", rejectedWorkerEnds.load(), int(device.lastError()));
    } else if (command == "direct_status") {
      const unsigned count = completions.load(std::memory_order_acquire);
      Serial.printf("DIRECT_STATUS %u status=%u requested=%u actual=%u known=%u pointer=%u\r\n",
                    count, unsigned(directResult.status), unsigned(directResult.requestedBytes),
                    unsigned(directResult.actualBytes), unsigned(directResult.actualBytesKnown),
                    unsigned(directResult.buffer == directBuffer && directResult.id != 0));
    } else if (command == "direct_invalid") {
      const unsigned before = completions.load();
      const bool nullRejected = !vendor.writeDirect(nullptr, 1);
      const auto alignment = vendor.writeDirect(directBuffer + 1, 1);
      const bool alignmentRejected = !alignment && alignment.error == EspUsbTransferError::NotAligned;
      const bool lengthRejected = !vendor.writeDirect(directBuffer, 65536);
      const auto packets = vendor.writeDirect(directBuffer, vendor.maxDirectTransfer() + 1);
      const bool packetsRejected = !packets && packets.error == EspUsbTransferError::BadArgument;
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
#ifdef ESPUSB_CANCEL_LIFECYCLE_FAULT
      lifecycleRelease.store(true);
#endif
      if (device.end() != ESP_OK) { Serial.println("STOP_FAILED"); return; }
#ifdef ESPUSB_CANCEL_FENCE_FAULT
      espusb::internal::removeTinyUsbRuntimeJob(faultFillJob);
#endif
#ifdef ESPUSB_CANCEL_LIFECYCLE_FAULT
      espusb::internal::removeTinyUsbRuntimeJob(lifecycleParkJob);
#endif
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
