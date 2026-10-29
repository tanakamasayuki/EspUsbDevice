#include <Arduino.h>
#include <EspUsbDevice.h>
#include <EspUsbHost.h>
#include <atomic>
#include <new>
#include "esp_heap_caps.h"

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
EspUsbHost host;
EspUsbDeviceCdcSerial *serialPort=nullptr;
EspUsbDeviceHidKeyboard *keyboard=nullptr;
std::atomic<unsigned> keyPresses{0},keyReleases{0};
std::atomic<size_t> serialCount{0};
uint8_t serialInput[8192];
std::atomic<unsigned> address{0}, speed{255}, echoed{0}, bad{0}, completed{0};
std::atomic<size_t> actual{0};
std::atomic<bool> txSuccess{false};
std::atomic<unsigned> rxCompleted{0};
std::atomic<size_t> rxActual{0};
std::atomic<bool> rxSuccess{false};
bool hostStarted = false, synchronousReader = false;
size_t lastReadBytes = 0;
std::atomic<uint8_t *> hostDestination{nullptr};
std::atomic<size_t> hostCapacity{0},hostReceived{0};
std::atomic<bool> hostOverflow{false};
alignas(64) uint8_t loan[8320];
uint8_t sent[8192], received[8192];

bool stopUsb() {
  if (hostStarted && address.load() && host.vendorReadQueueReady(address.load())) host.vendorReadQueueEnd(address.load());
  const bool stopped = device.end() == ESP_OK;
  if (hostStarted) host.end();
  hostStarted = false; address.store(0); hostDestination.store(nullptr);
  delete keyboard; keyboard=nullptr; delete serialPort; serialPort=nullptr;
  return stopped && heap_caps_check_integrity_all(true);
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (info.vid == 0x303a && info.pid == 0x4000 && strcmp(info.product, "v3 P4 loopback") == 0) {
      speed.store(info.speed); address.store(info.address);
    }
  });
  host.onVendorData([](const EspUsbHostVendorData &event) {
    if (!event.length) return;
    uint8_t *destination=hostDestination.load(std::memory_order_acquire);
    const size_t offset=hostReceived.load();
    if (destination && event.data && offset+event.length<=hostCapacity.load()) memcpy(destination+offset,event.data,event.length);
    else hostOverflow.store(true);
    hostReceived.store(offset+event.length,std::memory_order_release);
  });
  host.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
    if (event.keycode==4) { if(event.pressed) ++keyPresses; if(event.released) ++keyReleases; }
  });
  host.onSerialData([](const EspUsbHostSerialData &event) {
    const size_t offset=serialCount.load();
    if (offset+event.length<=sizeof(serialInput)) memcpy(serialInput+offset,event.data,event.length);
    else ++bad;
    serialCount.store(offset+event.length,std::memory_order_release);
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
void prepareReceiver(uint8_t *data,size_t length) {
  hostDestination.store(nullptr); hostReceived.store(0); hostCapacity.store(length); hostOverflow.store(false);
  if (!synchronousReader) hostDestination.store(data,std::memory_order_release);
}
bool readExactly(uint8_t *data, size_t length) {
  lastReadBytes = 0;
  const uint32_t deadline = millis() + 10000;
  if (!synchronousReader) {
    while (hostReceived.load(std::memory_order_acquire)<length && !hostOverflow.load() && int32_t(deadline-millis())>0) delay(1);
    lastReadBytes=hostReceived.load(std::memory_order_acquire);
    hostDestination.store(nullptr);
    return !hostOverflow.load() && lastReadBytes==length;
  }
  while (lastReadBytes < length && int32_t(deadline-millis()) > 0) {
    size_t n = 0;
    if (synchronousReader) {
      if (!host.vendorReadSync(data+lastReadBytes,length-lastReadBytes,&n,5000,address.load())) return false;
    } else n=host.vendorRead(data+lastReadBytes,length-lastReadBytes,address.load());
    if (n>length-lastReadBytes) return false;
    lastReadBytes += n;
    if (!n) taskYIELD(); // Keep the published Host 512-byte ring draining.
  }
  return lastReadBytes == length;
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
    prepareReceiver(tx+64,n);
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
  if (!ok) stopUsb(); // Return both Device DMA and Host callback ownership before freeing.
  hostDestination.store(nullptr);
  heap_caps_free(rx); heap_caps_free(tx);
  ok &= heap_caps_check_integrity_all(true);
  Serial.printf("BORROWED ok=%u rx=%u tx=%u bytes=%u guards=%u actual=%u completed=%u\r\n",unsigned(ok),receives,sends,unsigned(total),unsigned(guards),unsigned(rxActual.load()),rxCompleted.load());
}

void exerciseComposite() {
  bool ok = serialPort && keyboard && device.ready() && host.serialReady(address.load(),0);
  unsigned exchanges=0; size_t bytes=0;
  if (ok) ok &= host.setSerialBaudRate(230400,address.load(),0);
  uint32_t deadline=millis()+2000;
  while (ok && serialPort->lineCoding().baud!=230400 && int32_t(deadline-millis())>0) delay(1);
  ok &= serialPort && serialPort->lineCoding().baud==230400;
  const size_t lengths[]={1,64,129,513};
  for (size_t n : lengths) {
    if (!ok) break;
    for (size_t i=0;i<n;++i) sent[i]=uint8_t(n+i*31);
    serialCount.store(0,std::memory_order_release);
    ok &= host.sendSerial(sent,n,address.load(),0);
    deadline=millis()+5000;
    while (ok && serialCount.load(std::memory_order_acquire)<n && int32_t(deadline-millis())>0) delay(1);
    ok &= serialCount.load(std::memory_order_acquire)==n && memcmp(serialInput,sent,n)==0;
    if (ok) { ++exchanges; bytes+=n; }
  }
  for (unsigned i=0;i<3 && ok;++i) {
    ok &= keyboard->pressKey('a');
    deadline=millis()+2000;
    while (ok && keyPresses.load()<i+1 && int32_t(deadline-millis())>0) delay(1);
    ok &= keyPresses.load()==i+1;
    ok &= keyboard->releaseAll();
    deadline=millis()+2000;
    while (ok && keyReleases.load()<i+1 && int32_t(deadline-millis())>0) delay(1);
    ok &= keyReleases.load()==i+1;
  }
  ok &= bad.load()==0 && heap_caps_check_integrity_all(true);
  Serial.printf("COMPOSITE ok=%u cdc=%u bytes=%u press=%u release=%u baud=%u bad=%u\r\n",unsigned(ok),exchanges,unsigned(bytes),keyPresses.load(),keyReleases.load(),serialPort?unsigned(serialPort->lineCoding().baud):0,bad.load());
}

void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start fs") || command.startsWith("start hs")) {
    const bool hs = command.startsWith("start hs");
    const bool borrowed = command.indexOf(" borrowed")>=0;
    const bool composite = command.indexOf(" composite")>=0;
    synchronousReader=command.endsWith(" sync");
    keyPresses.store(0); keyReleases.store(0); serialCount.store(0);
    address.store(0); speed.store(255); echoed.store(0); bad.store(0);
    EspUsbVendorConfig buffers; buffers.rxCapacity = 8192; buffers.txCapacity = 8192; buffers.transferBytes = 64;
    if (borrowed) { buffers.rxCapacity=0; buffers.txCapacity=0; buffers.rxMode=EspUsbBulkRxMode::Borrowed; }
    EspUsbDeviceConfig dc; dc.product = "v3 P4 loopback";
    dc.controller = hs ? EspUsbController::HighSpeed : EspUsbController::FullSpeed;
    EspUsbHostConfig hc; hc.port = hs ? ESP_USB_HOST_PORT_FULL_SPEED : ESP_USB_HOST_PORT_HIGH_SPEED;
    bool ok = vendor.configure(buffers) == ESP_OK;
    if (composite) {
      serialPort=new(std::nothrow) EspUsbDeviceCdcSerial(device);
      keyboard=new(std::nothrow) EspUsbDeviceHidKeyboard(device);
      ok &= serialPort && keyboard;
      EspUsbBulkStreamConfig cb; cb.rxCapacity=1024; cb.txCapacity=1024; cb.transferBytes=64;
      if (serialPort) {
        ok &= serialPort->configure(cb)==ESP_OK;
        ok &= serialPort->onRx([](size_t) {
          uint8_t data[64];
          while (serialPort->available() && serialPort->writeAvailable()) {
            const size_t n=serialPort->read(data,min(sizeof(data),serialPort->writeAvailable()));
            if (!n) break;
            if(serialPort->write(data,n)!=n) ++bad;
          }
        })==ESP_OK;
      }
    }
    hostStarted = host.begin(hc); ok &= hostStarted;
    ok &= device.begin(dc);
    const uint32_t deadline = millis() + 12000;
    while (ok && (!address.load() || !device.ready()) && int32_t(deadline - millis()) > 0) delay(1);
    ok &= address.load() && device.ready();
    if (ok) ok &= host.vendorOpen(address.load(), 0xff, synchronousReader ? ESP_USB_HOST_VENDOR_READ_ON_DEMAND : ESP_USB_HOST_VENDOR_READ_CONTINUOUS,64);
    if (ok && !synchronousReader) ok &= host.vendorReadQueueBegin(1,64,address.load());
    const uint32_t serialDeadline=millis()+5000;
    while (ok && composite && !host.serialReady(address.load(),0) && int32_t(serialDeadline-millis())>0) delay(1);
    if (composite) ok &= host.serialReady(address.load(),0);
    const unsigned in = ok ? host.vendorInPacketSize(address.load()) : 0;
    const unsigned out = ok ? host.vendorOutPacketSize(address.load()) : 0;
    Serial.printf("START ok=%u device=%s speed=%u in=%u out=%u error=%d\r\n", unsigned(ok), hs ? "hs" : "fs", speed.load(), in, out, int(device.lastError()));
  } else if (command == "composite") {
    exerciseComposite();
  } else if (command == "borrowed") {
    exerciseBorrowed();
  } else if (command == "exercise") {
    bool ok = address.load() && device.ready(); unsigned transfers = 0, directs = 0; size_t total = 0;
    const size_t lengths[] = {1,63,64,65,127,128,129,511,512,513,4097};
    for (size_t n : lengths) {
      if (!ok) break;
      for (size_t i=0; i<n; ++i) sent[i] = uint8_t(i*37+n);
      memset(received, 0, n); size_t got = 0;
      prepareReceiver(received,n);
      ok &= host.vendorWrite(sent, n, address.load());
      if (ok) ok &= readExactly(received,n); got=lastReadBytes;
      ok &= got == n && memcmp(sent, received, n) == 0;
      if (ok) { ++transfers; total += n; }
    }
    const size_t directLengths[] = {65,129,513,4097};
    for (unsigned repeat=0;repeat<32 && ok;++repeat) for (size_t n : directLengths) {
      if (!ok) break;
      memset(loan, 0xa5, sizeof(loan));
      for (size_t i=0; i<n; ++i) loan[64+i] = uint8_t(i*19+n);
      actual.store(0); completed.store(0); txSuccess.store(false);
      prepareReceiver(received,n);
      const auto submission = vendor.writeDirect(loan+64, n);
      size_t got = 0; ok &= bool(submission);
      if (ok) ok &= readExactly(received,n); got=lastReadBytes;
      const uint32_t deadline = millis()+1000;
      while (ok && !completed.load() && int32_t(deadline-millis())>0) delay(1);
      ok &= got == n && completed.load()==1 && txSuccess.load() && actual.load()==n && memcmp(received,loan+64,n)==0;
      for (size_t i=0;i<64;++i) ok &= loan[i]==0xa5;
      for (size_t i=64+n;i<sizeof(loan);++i) ok &= loan[i]==0xa5;
      if (!ok) {
        size_t mismatch=0; while(mismatch<n && received[mismatch]==loan[64+mismatch]) ++mismatch;
        Serial.printf("DIRECT_DATA offset=%u received=%02x loan=%02x expected=%02x\r\n",unsigned(mismatch),unsigned(mismatch<n?received[mismatch]:0),unsigned(mismatch<n?loan[64+mismatch]:0),unsigned(uint8_t(mismatch*19+n)));
      }
      if (!ok) Serial.printf("DIRECT_FAIL n=%u accepted=%u error=%u got=%u completed=%u success=%u actual=%u equal=%u\r\n",unsigned(n),unsigned(bool(submission)),unsigned(submission.error),unsigned(got),completed.load(),unsigned(txSuccess.load()),unsigned(actual.load()),unsigned(got==n && memcmp(received,loan+64,n)==0));
      if (ok) ++directs;
    }
    ok &= bad.load()==0 && echoed.load()==total && heap_caps_check_integrity_all(true);
    Serial.printf("EXERCISE ok=%u copies=%u bytes=%u direct=%u echoed=%u bad=%u guards=1\r\n", unsigned(ok),transfers,unsigned(total),directs,echoed.load(),bad.load());
  } else if (command == "stop") {
    Serial.println(stopUsb() ? "STOPPED" : "STOP_FAILED");
  }
}
