#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
#include "esp_heap_caps.h"
// Vendor Device on the high-speed controller: copied echo, borrowed RX/TX with
// guards, and the single return of a loan on cancel and on end().
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
constexpr size_t GUARD = 64, LARGEST = 65537, STORAGE = 66048 + 2 * GUARD;
uint8_t *rx = nullptr, *tx = nullptr;
std::atomic<unsigned> rxDone{0}, txDone{0}, bad{0};
std::atomic<size_t> echoed{0};
EspUsbTransferResult rxResult, txResult;
bool borrowed = false;

uint8_t pattern(size_t i, size_t n) { return uint8_t(i * 29 + n); }
size_t posted(size_t n) { return (n + 511) / 512 * 512; }
bool guardsKept(const uint8_t *buffer, size_t from) {
  bool ok = true;
  for (size_t i = 0; i < GUARD; ++i) ok &= buffer[i] == 0xa5;
  for (size_t i = from; i < STORAGE; ++i) ok &= buffer[i] == 0xa5;
  return ok;
}
bool waitFor(std::atomic<unsigned> &count, unsigned target) {
  const uint32_t begun = millis();
  while (count.load() < target && millis() - begun < 10000) delay(1);
  delay(20);  // a second completion would arrive here
  return count.load() == target;
}
const char *name(EspUsbTransferStatus status) {
  switch (status) {
    case EspUsbTransferStatus::Success: return "success";
    case EspUsbTransferStatus::BusReset: return "reset";
    case EspUsbTransferStatus::Stopped: return "stopped";
    case EspUsbTransferStatus::TransferError: return "error";
    default: return "cancelled";
  }
}
void release() {
  heap_caps_free(rx); heap_caps_free(tx); rx = tx = nullptr;
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  vendor.onRx([](size_t) {
    uint8_t data[512];
    while (vendor.available() && vendor.writeAvailable()) {
      const size_t n = vendor.read(data, min(sizeof(data), vendor.writeAvailable()));
      if (!n) break;
      if (vendor.write(data, n) != n) ++bad;
      echoed.fetch_add(n);
    }
  });
  vendor.onDirectRxComplete([](const EspUsbTransferResult &result) { rxResult = result; ++rxDone; });
  vendor.onDirectTxComplete([](const EspUsbTransferResult &result) { txResult = result; ++txDone; });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  const size_t n = command.indexOf(' ') > 0 ? command.substring(command.indexOf(' ') + 1).toInt() : 0;
  if (command.startsWith("start ")) {
    borrowed = command.endsWith("borrowed");
    echoed = 0; bad = 0; rxDone = 0; txDone = 0;
    EspUsbVendorConfig config; config.rxCapacity = 16384; config.txCapacity = 16384; config.transferBytes = 512;
    if (borrowed) { config.rxCapacity = 0; config.txCapacity = 0; config.rxMode = EspUsbBulkRxMode::Borrowed; }
    bool ok = vendor.configure(config) == ESP_OK;
    if (borrowed) {
      rx = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, STORAGE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
      tx = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, STORAGE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
      ok &= rx && tx;
    }
    EspUsbDeviceConfig identity; identity.vid = 0x1209; identity.pid = 0x0003;
    identity.product = "EspUsbDevice vendor link"; identity.controller = EspUsbController::HighSpeed;
    ok &= device.begin(identity);
    Serial.printf("STARTED %u\r\n", unsigned(ok));
  } else if (command == "link") {
    // Valid once the Host has configured the device. Borrowed RX exists only in that mode.
    Serial.printf("LINK ready=%u maxrx=%u maxtx=%u\r\n", unsigned(device.ready()),
                  unsigned(vendor.maxDirectReceive() >= posted(LARGEST)), unsigned(vendor.maxDirectTransfer() >= LARGEST));
  } else if (command == "echoed") {
    Serial.printf("ECHOED %u bad=%u\r\n", unsigned(echoed.load()), bad.load());
  } else if (command.startsWith("rx ")) {
    memset(rx, 0xa5, STORAGE); rxDone = 0;
    const auto submission = vendor.readDirect(rx + GUARD, posted(n));
    Serial.printf("RX_ARMED %u ok=%u\r\n", unsigned(n), unsigned(bool(submission)));
  } else if (command.startsWith("rx_done ")) {
    bool ok = waitFor(rxDone, 1) && rxResult.status == EspUsbTransferStatus::Success && rxResult.actualBytesKnown &&
              rxResult.actualBytes == n && rxResult.buffer == rx + GUARD;
    for (size_t i = 0; i < n && ok; ++i) ok &= rx[GUARD + i] == pattern(i, n);
    // The whole posted span is DMA-owned; only bytes past it must keep the guard.
    ok &= guardsKept(rx, GUARD + posted(n));
    if (!ok) {
      size_t at = 0; while (at < n && rx[GUARD + at] == pattern(at, n)) ++at;
      Serial.printf("RX_MISMATCH at=%u got=%02x want=%02x guards=%u status=%s known=%u buffer=%u\r\n", unsigned(at),
                    unsigned(at < n ? rx[GUARD + at] : 0), unsigned(at < n ? pattern(at, n) : 0),
                    unsigned(guardsKept(rx, GUARD + posted(n))), name(rxResult.status), unsigned(rxResult.actualBytesKnown),
                    unsigned(rxResult.buffer == rx + GUARD));
    }
    Serial.printf("RX_DONE %u ok=%u actual=%u completions=%u\r\n", unsigned(n), unsigned(ok), unsigned(rxResult.actualBytes), rxDone.load());
  } else if (command.startsWith("tx ")) {
    memset(tx, 0xa5, STORAGE);
    memcpy(tx + GUARD, rx + GUARD, n);
    txDone = 0;
    const auto submission = vendor.writeDirect(tx + GUARD, n);
    bool ok = bool(submission) && waitFor(txDone, 1) && txResult.status == EspUsbTransferStatus::Success &&
              txResult.actualBytesKnown && txResult.actualBytes == n && txResult.id == submission.id;
    ok &= guardsKept(tx, GUARD + n);
    for (size_t i = 0; i < n && ok; ++i) ok &= tx[GUARD + i] == pattern(i, n);
    Serial.printf("TX_DONE %u ok=%u completions=%u\r\n", unsigned(n), unsigned(ok), txDone.load());
  } else if (command == "cancel") {
    // A loan the Host never fills, returned once by cancellation.
    memset(rx, 0xa5, STORAGE); rxDone = 0;
    const auto submission = vendor.readDirect(rx + GUARD, 4096);
    const bool requested = vendor.cancelDirectRx(submission.id) == ESP_OK;
    const bool once = waitFor(rxDone, 1);
    Serial.printf("CANCEL ok=%u requested=%u completions=%u status=%s id=%u\r\n",
                  unsigned(bool(submission) && once && rxResult.id == submission.id), unsigned(requested), rxDone.load(),
                  name(rxResult.status), unsigned(rxResult.id == submission.id));
  } else if (command == "end_pending") {
    // A loan outstanding at end(): returned once with Stopped before end() returns.
    memset(rx, 0xa5, STORAGE); rxDone = 0;
    const auto submission = vendor.readDirect(rx + GUARD, 4096);
    const bool ended = device.end() == ESP_OK;
    const unsigned atEnd = rxDone.load();
    const bool once = waitFor(rxDone, 1);
    Serial.printf("END_PENDING ok=%u ended=%u at_end=%u completions=%u status=%s\r\n",
                  unsigned(bool(submission) && ended && once && rxResult.status == EspUsbTransferStatus::Stopped),
                  unsigned(ended), atEnd, rxDone.load(), name(rxResult.status));
  } else if (command == "stop") {
    const bool stopped = !device.started() || device.end() == ESP_OK;
    release();
    Serial.printf("STOPPED %u heap=%u\r\n", unsigned(stopped), unsigned(heap_caps_check_integrity_all(true)));
  }
}
