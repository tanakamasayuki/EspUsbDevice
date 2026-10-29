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
  vendor.onDirectTxComplete([](const EspUsbTransferResult &result) {
    actual.store(result.actualBytes);
    txSuccess.store(result.status == EspUsbTransferStatus::Success && result.actualBytesKnown);
    ++completed;
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start fs" || command == "start hs") {
    const bool hs = command.endsWith("hs");
    address.store(0); speed.store(255); echoed.store(0); bad.store(0);
    EspUsbVendorConfig buffers; buffers.rxCapacity = 8192; buffers.txCapacity = 8192; buffers.transferBytes = 64;
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
