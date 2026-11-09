#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
std::atomic<uint8_t> address{0};
uint8_t buffer[8 * 512], expected[8 * 512];
std::atomic<bool> echoRunning{false},echoExited{true},echoBad{false},storageActive{false};
std::atomic<unsigned> echoCount{0},echoBytes{0},overlap{0};
std::atomic<size_t> serialCount{0};
uint8_t serialInput[256];
void echoTask(void *) {
  unsigned sequence = 0;
  const unsigned lengths[] = {1,63,64,65,129};
  while (echoRunning.load() && !echoBad.load()) {
    uint8_t data[129]; const unsigned n = lengths[sequence % 5];
    for (unsigned i = 0; i < n; ++i) data[i] = uint8_t(i*37 + sequence);
    serialCount.store(0,std::memory_order_release);
    bool ok = host.sendSerial(data,n,address.load(),0);
    const uint32_t until = millis()+3000;
    while (ok && serialCount.load(std::memory_order_acquire)<n && int32_t(until-millis())>0) delay(1);
    ok &= serialCount.load(std::memory_order_acquire)==n && !memcmp(serialInput,data,n);
    if (!ok) { echoBad=true; break; }
    ++echoCount; echoBytes.fetch_add(n);
    if (storageActive.load()) ++overlap;
    ++sequence; delay(1);
  }
  echoExited=true; vTaskDelete(nullptr);
}
bool stopEcho() {
  echoRunning=false; const uint32_t until=millis()+4000;
  while(!echoExited.load() && int32_t(until-millis())>0) delay(1);
  return echoExited.load();
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onSerialData([](const EspUsbHostSerialData &d) {
    if(d.port || !d.length) return;
    const size_t n=serialCount.load(std::memory_order_relaxed);
    if(n+d.length>sizeof(serialInput)){echoBad=true;return;}
    memcpy(serialInput+n,d.data,d.length);
    serialCount.store(n+d.length,std::memory_order_release);
  });
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &d) { if (d.vid == 0x303a && d.pid == 0x4000) address.store(d.address); });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    address.store(0); bool ok = host.begin(); const uint32_t until = millis() + 10000;
    while (ok && (!address.load() || !host.mscReady(address.load())) && int32_t(until - millis()) > 0) delay(1);
    EspUsbHostMscInquiry inquiry; uint32_t count = 0, size = 0; uint8_t lun = 255;
    ok &= address.load() && host.mscInquiry(inquiry, address.load()) && strncmp(inquiry.vendor, "V3TEST", 6) == 0;
    ok &= host.mscCapacity(count, size, address.load()) && count == 32 && size == 512;
    ok &= host.mscMaxLun(lun, address.load()) && !lun;
    Serial.printf("HOST_READY %u\r\n", unsigned(ok));
  } else if (command == "roundtrip") {
    bool ok = true;
    for (unsigned blocks : {1u, 2u, 7u, 8u}) {
      for (unsigned i = 0; i < blocks * 512; ++i) expected[i] = uint8_t(i * 37 + blocks);
      storageActive=true;
      ok &= host.mscWriteBlocks(3, expected, blocks, address.load());
      storageActive=false;
      memset(buffer, 0, sizeof(buffer));
      storageActive=true;
      ok &= host.mscReadBlocks(3, buffer, blocks, address.load()) && !memcmp(buffer, expected, blocks * 512);
      storageActive=false;
    }
    ok &= host.mscSynchronizeCache(address.load());
    Serial.printf("ROUNDTRIP %u\r\n", unsigned(ok));
  } else if (command.startsWith("error ")) {
    unsigned mode = command.substring(6).toInt(); bool ok = false;
    if (mode == 0) ok = !host.mscWriteBlocks(3, expected, 1, address.load());
    if (mode == 1) ok = !host.mscTestUnitReady(address.load());
    if (mode == 2) ok = !host.mscReadBlocks(3, buffer, 1, address.load());
    if (mode != 1) { Serial.printf("ERROR_REJECTED %u\r\n", unsigned(ok)); return; }
    EspUsbHostMscSense sense;
    const uint8_t key = mode == 0 ? 7 : mode == 1 ? 2 : 3;
    const uint8_t asc = mode == 0 ? 0x27 : mode == 1 ? 0x3a : 0x11;
    ok &= host.mscLastSense(sense, address.load()) && sense.senseKey == key && sense.additionalSenseCode == asc;
    Serial.printf("ERROR_SENSE %u key=%u asc=%u\r\n", unsigned(ok), unsigned(sense.senseKey), unsigned(sense.additionalSenseCode));
  } else if (command == "read") {
    Serial.printf("READ %u\r\n", unsigned(host.mscReadBlocks(3, buffer, 1, address.load())));
  } else if (command == "changed") {
    // The first command after a reinsertion reports UNIT ATTENTION 06/28 once.
    EspUsbHostMscSense sense; bool ok = !host.mscTestUnitReady(address.load());
    ok &= host.mscLastSense(sense, address.load()) && sense.senseKey == 6 && sense.additionalSenseCode == 0x28;
    Serial.printf("CHANGED_SENSE %u key=%u asc=%u\r\n", unsigned(ok), unsigned(sense.senseKey), unsigned(sense.additionalSenseCode));
  } else if (command == "ready") {
    Serial.printf("READY %u\r\n", unsigned(host.mscTestUnitReady(address.load())));
  } else if (command == "cdc_start") {
    bool ok=echoExited.load() && address.load() && host.serialReady(address.load(),0);
    if(ok) {
      echoBad=false; echoCount=0; echoBytes=0; overlap=0; serialCount=0;
      echoExited=false; echoRunning=true;
      ok=xTaskCreate(echoTask,"cdc-msc-echo",4096,nullptr,2,nullptr)==pdPASS;
      if(!ok){echoRunning=false;echoExited=true;}
    }
    Serial.printf("CDC_STARTED %u\r\n",unsigned(ok));
  } else if (command == "cdc_progress") {
    const unsigned before=echoCount.load(); const uint32_t until=millis()+5000;
    while(echoCount.load()<before+5 && !echoBad.load() && int32_t(until-millis())>0) delay(1);
    Serial.printf("CDC_PROGRESS ok=%u echoes=%u overlap=%u\r\n",unsigned(echoCount.load()>=before+5 && !echoBad.load()),echoCount.load(),overlap.load());
  } else if (command == "cdc_stop") {
    const bool ok=stopEcho();
    Serial.printf("CDC_RESULT stopped=%u echoes=%u bytes=%u overlap=%u bad=%u\r\n",unsigned(ok),echoCount.load(),echoBytes.load(),overlap.load(),unsigned(echoBad.load()));
  } else if (command == "stop") {
    if(!stopEcho()){Serial.println("STOP_FAILED");return;}
    host.end(); Serial.println(!host.ready() ? "STOPPED" : "STOP_FAILED");
  }
}
