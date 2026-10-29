#include <Arduino.h>
#include <EspUsbDevice.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
EspUsbHost host;
std::atomic<unsigned> address{0}, speed{255}, echoed{0}, bad{0}, completed{0};
std::atomic<size_t> actual{0};
std::atomic<bool> txSuccess{false};
std::atomic<unsigned> rxCompleted{0};
std::atomic<size_t> rxActual{0};
std::atomic<bool> rxSuccess{false};
bool hostStarted = false;
alignas(64) uint8_t loan[8320];
uint8_t sent[8192], received[8192];

bool stopUsb() {
  const bool stopped = device.end() == ESP_OK;
  if (hostStarted) host.end();
  hostStarted = false; address.store(0);
  return stopped && heap_caps_check_integrity_all(true);
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (info.vid == 0x303a && info.pid == 0x4000 && strcmp(info.product, "v3 P4 loopback") == 0) {
      speed.store(info.speed); address.store(info.address);
    }
  });
  vendor.onRx([](size_t) {
    uint8_t data[64];
    while (vendor.available() && vendor.writeAvailable()) {
      const size_t n = vendor.read(data, min(sizeof(data), vendor.writeAvailable()));
      if (!n) break;
      if (vendor.write(data, n) != n) ++bad;
      echoed.fetch_add(n);
    }
  });
  vendor.onDirectRxComplete([](const EspUsbTransferResult &result) {
    rxActual.store(result.actualBytes);
    rxSuccess.store(result.status == EspUsbTransferStatus::Success && result.actualBytesKnown);
    ++rxCompleted;
  });
  vendor.onDirectTxComplete([](const EspUsbTransferResult &result) {
    actual.store(result.actualBytes);
    txSuccess.store(result.status == EspUsbTransferStatus::Success && result.actualBytesKnown);
    ++completed;
  });
}
bool readExactly(uint8_t *data, size_t length) {
  size_t total = 0;
  const uint32_t deadline = millis() + 10000;
  while (total < length && int32_t(deadline-millis()) > 0) {
    size_t n = 0;
    if (!host.vendorReadSync(data+total, length-total, &n, 5000, address.load()) || !n || n>length-total) return false;
    total += n;
  }
  return total == length;
}
void exerciseBorrowed() {
  constexpr size_t capacity = 65600, storage = capacity+128;
  auto *rx = static_cast<uint8_t *>(heap_caps_aligned_alloc(64,storage,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
  auto *tx = static_cast<uint8_t *>(heap_caps_aligned_alloc(64,storage,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
  bool ok = rx && tx && device.ready(), guards = true;
  unsigned receives=0, sends=0; size_t total=0;
  const size_t lengths[] = {1,63,64,65,129,513,4097,65537};
  for (size_t n : lengths) {
    if (!ok) break;
    memset(rx,0xa5,storage); memset(tx,0xa5,storage);
    for (size_t i=0;i<n;++i) tx[64+i]=uint8_t(i*29+n);
    rxCompleted.store(0); rxActual.store(0); rxSuccess.store(false);
    const auto posted=vendor.readDirect(rx+64,(n+63)/64*64);
    ok &= bool(posted);
    if (ok) ok &= host.vendorWrite(tx+64,n,address.load());
    uint32_t deadline=millis()+10000;
    while (ok && !rxCompleted.load() && int32_t(deadline-millis())>0) delay(1);
    ok &= rxCompleted.load()==1 && rxSuccess.load() && rxActual.load()==n;
    if (ok) ++receives;
    for (size_t i=0;i<n && ok;++i) ok &= rx[64+i]==uint8_t(i*29+n);
    if (!ok) break;
    memset(tx+64,0,n);
    completed.store(0); actual.store(0); txSuccess.store(false);
    const auto sent=vendor.writeDirect(rx+64,n);
    ok &= bool(sent);
    if (ok) ok &= readExactly(tx+64,n);
    deadline=millis()+1000;
    while (ok && !completed.load() && int32_t(deadline-millis())>0) delay(1);
    ok &= completed.load()==1 && txSuccess.load() && actual.load()==n;
    for (size_t i=0;i<n && ok;++i) ok &= tx[64+i]==uint8_t(i*29+n) && rx[64+i]==tx[64+i];
    for (size_t i=0;i<64;++i) guards &= rx[i]==0xa5 && tx[i]==0xa5;
    // RX owns the whole posted span, including short-packet padding.
    for (size_t i=64+(n+63)/64*64;i<storage;++i) guards &= rx[i]==0xa5;
    for (size_t i=64+n;i<storage;++i) guards &= tx[i]==0xa5;
    ok &= guards;
    if (ok) { ++sends; total+=n; }
  }
  if (!ok) device.end(); // Return any outstanding DMA loan before freeing it.
  heap_caps_free(rx); heap_caps_free(tx);
  ok &= heap_caps_check_integrity_all(true);
  Serial.printf("BORROWED ok=%u rx=%u tx=%u bytes=%u guards=%u actual=%u completed=%u\r\n",unsigned(ok),receives,sends,unsigned(total),unsigned(guards),unsigned(rxActual.load()),rxCompleted.load());
}

void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start fs") || command.startsWith("start hs")) {
    const bool hs = command.startsWith("start hs");
    const bool borrowed = command.endsWith(" borrowed");
    address.store(0); speed.store(255); echoed.store(0); bad.store(0);
    EspUsbVendorConfig buffers; buffers.rxCapacity = 8192; buffers.txCapacity = 8192; buffers.transferBytes = 64;
    if (borrowed) { buffers.rxCapacity=0; buffers.txCapacity=0; buffers.rxMode=EspUsbBulkRxMode::Borrowed; }
    EspUsbDeviceConfig dc; dc.product = "v3 P4 loopback";
    dc.controller = hs ? EspUsbController::HighSpeed : EspUsbController::FullSpeed;
    EspUsbHostConfig hc; hc.port = hs ? ESP_USB_HOST_PORT_FULL_SPEED : ESP_USB_HOST_PORT_HIGH_SPEED;
    bool ok = vendor.configure(buffers) == ESP_OK;
    hostStarted = host.begin(hc); ok &= hostStarted;
    ok &= device.begin(dc);
    const uint32_t deadline = millis() + 12000;
    while (ok && (!address.load() || !device.ready()) && int32_t(deadline - millis()) > 0) delay(1);
    ok &= address.load() && device.ready();
    if (ok) ok &= host.vendorOpen(address.load(), 0xff, ESP_USB_HOST_VENDOR_READ_ON_DEMAND);
    const unsigned in = ok ? host.vendorInPacketSize(address.load()) : 0;
    const unsigned out = ok ? host.vendorOutPacketSize(address.load()) : 0;
    Serial.printf("START ok=%u device=%s speed=%u in=%u out=%u error=%d\r\n", unsigned(ok), hs ? "hs" : "fs", speed.load(), in, out, int(device.lastError()));
  } else if (command == "borrowed") {
    exerciseBorrowed();
  } else if (command == "exercise") {
    bool ok = address.load() && device.ready(); unsigned transfers = 0, directs = 0; size_t total = 0;
    const size_t lengths[] = {1,63,64,65,127,128,129,511,512,513,4097};
    for (size_t n : lengths) {
      if (!ok) break;
      for (size_t i=0; i<n; ++i) sent[i] = uint8_t(i*37+n);
      memset(received, 0, n); size_t got = 0;
      ok &= host.vendorWrite(sent, n, address.load());
      ok &= host.vendorReadSync(received, n, &got, 5000, address.load());
      ok &= got == n && memcmp(sent, received, n) == 0;
      if (ok) { ++transfers; total += n; }
    }
    const size_t directLengths[] = {65,129,513,4097};
    for (size_t n : directLengths) {
      if (!ok) break;
      memset(loan, 0xa5, sizeof(loan));
      for (size_t i=0; i<n; ++i) loan[64+i] = uint8_t(i*19+n);
      actual.store(0); completed.store(0); txSuccess.store(false);
      const auto submission = vendor.writeDirect(loan+64, n);
      size_t got = 0; ok &= bool(submission);
      if (ok) ok &= host.vendorReadSync(received, n, &got, 5000, address.load());
      const uint32_t deadline = millis()+1000;
      while (ok && !completed.load() && int32_t(deadline-millis())>0) delay(1);
      ok &= got == n && completed.load()==1 && txSuccess.load() && actual.load()==n && memcmp(received,loan+64,n)==0;
      for (size_t i=0;i<64;++i) ok &= loan[i]==0xa5;
      for (size_t i=64+n;i<sizeof(loan);++i) ok &= loan[i]==0xa5;
      if (ok) ++directs;
    }
    ok &= bad.load()==0 && echoed.load()==total && heap_caps_check_integrity_all(true);
    Serial.printf("EXERCISE ok=%u copies=%u bytes=%u direct=%u echoed=%u bad=%u guards=1\r\n", unsigned(ok),transfers,unsigned(total),directs,echoed.load(),bad.load());
  } else if (command == "stop") {
    Serial.println(stopUsb() ? "STOPPED" : "STOP_FAILED");
  }
}
