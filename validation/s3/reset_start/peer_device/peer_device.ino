#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
#include "../../../../src/internal/EspUsbController.h"
#include "../../../../src/internal/EspUsbDeviceBus.h"
#include "../../../../src/internal/EspUsbDeviceCore.h"
#include "../../../../src/internal/EspUsbDwc2Hardware.h"
#include "../../../../src/internal/EspUsbTinyUsbRuntime.h"
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
alignas(64) uint8_t oldTx[129],oldRx[192],newTx[64],newRx[64];
std::atomic<bool> monitor{false},parked{false},released{true};
std::atomic<unsigned> seen{0},beforeMounted{0},afterMounted{0},afterReady{0},oldReturns{0},successes{0},bad{0};
uint64_t oldTxId=0,oldRxId=0,newTxId=0,newRxId=0;
uint8_t input=0;std::atomic<unsigned> openBlocked{0},dataBlocked{0},controlBlocked{0};
intr_handler_t originalHandler=nullptr;void *originalContext=nullptr;
void observeInterrupt(void *) {
  const uint8_t port=uint8_t(uintptr_t(originalContext));
  const auto *regs=espusb_dwc2_registers(port);
  const uint32_t pending=regs->gintsts & regs->gintmsk;
  const bool sample=monitor.load() && (pending&GINTSTS_USBRST) && !(pending&GINTSTS_ENUMDNE);
  const bool mounted=sample && espusb_device_mounted();
  originalHandler(originalContext);
  if(sample && !seen.load()) {
    beforeMounted=mounted;afterMounted=espusb_device_mounted();
    afterReady=espusb::internal::usbDeviceCoreReadyForJobs();
    const EspUsbEndpointDescriptor endpoint={7,5,input,2,64,0};
    openBlocked=!espusb_controller_edpt_open(port,&endpoint);
    dataBlocked=!espusb_controller_edpt_xfer(port,input,oldTx,64,true);
    controlBlocked=!espusb_controller_edpt_xfer(port,128,oldTx,64,true);
    ++seen;
  }
}
extern "C" esp_err_t espusb_dwc2_allocate_interrupt(int source,int flags,intr_handler_t handler,void *context,intr_handle_t *result) {
  originalHandler=handler;originalContext=context;
  return esp_intr_alloc(source,flags,observeInterrupt,nullptr,result);
}
void park(void *) {
  parked=true;const uint32_t until=millis()+10000;
  while(!released.load() && int32_t(until-millis())>0)delay(1);
  if(!released.load())++bad;
  parked=false;
}
espusb::internal::TinyUsbRuntimeJob parkJob{park,nullptr};
void setup() {
  Serial.begin(115200);Serial.setTimeout(100);
  memset(oldTx,0xdd,sizeof(oldTx));memset(oldRx,0xcc,sizeof(oldRx));
  for(unsigned i=0;i<64;++i)newTx[i]=uint8_t(i*37+13);
  vendor.onDirectTxComplete([](const EspUsbTransferResult &r){
    if(r.buffer==oldTx){if(r.id!=oldTxId || r.status!=EspUsbTransferStatus::BusReset)++bad;++oldReturns;}
    else if(r.buffer==newTx){if(r.id!=newTxId || r.status!=EspUsbTransferStatus::Success || !r.actualBytesKnown || r.actualBytes!=64)++bad;++successes;}
    else ++bad;
  });
  vendor.onDirectRxComplete([](const EspUsbTransferResult &r){
    if(r.buffer==oldRx){if(r.id!=oldRxId || r.status!=EspUsbTransferStatus::BusReset)++bad;++oldReturns;}
    else if(r.buffer==newRx){
      if(r.id!=newRxId || r.status!=EspUsbTransferStatus::Success || !r.actualBytesKnown || r.actualBytes!=64)++bad;
      for(unsigned i=0;i<64;++i)if(newRx[i]!=uint8_t(i*17+7))++bad;++successes;
    } else ++bad;
  });
}
void loop() {
  if(!Serial.available()){delay(1);return;}String s=Serial.readStringUntil('\n');s.trim();
  if(s.startsWith("start ")) {
    oldReturns=successes=bad=0;monitor=false;released=true;seen=0;
    EspUsbBulkStreamConfig cfg;cfg.rxMode=EspUsbBulkRxMode::Borrowed;cfg.rxCapacity=cfg.txCapacity=cfg.transferBytes=0;
    EspUsbDeviceConfig identity;identity.eventQueueDepth=s.substring(6).toInt();
    bool ok=vendor.configure(cfg)==ESP_OK && device.begin(identity);input=0;
    const auto *d=device.configurationDescriptor(0);const unsigned length=d ? d[2]|unsigned(d[3])<<8:0;
    for(unsigned n=9;n+2<=length && d[n]>=2 && d[n]<=length-n;n+=d[n])if(d[n+1]==5 && (d[n+2]&128))input=d[n+2];
    Serial.printf("STARTED %u\r\n",unsigned(ok && input));
  } else if(s=="prime") {
    auto tx=vendor.writeDirect(oldTx,sizeof(oldTx));oldTxId=tx.id;
    auto rx=vendor.readDirect(oldRx,sizeof(oldRx));oldRxId=rx.id;
    Serial.printf("PRIMED %u\r\n",unsigned(bool(tx) && bool(rx)));
  } else if(s=="park") {
    released=false;seen=beforeMounted=afterMounted=afterReady=openBlocked=dataBlocked=controlBlocked=0;
    bool ok=espusb::internal::postTinyUsbRuntimeJob(parkJob);const uint32_t until=millis()+2000;
    while(ok && !parked.load() && int32_t(until-millis())>0)delay(1);
    monitor=ok && parked.load();Serial.printf("PARKED %u\r\n",unsigned(monitor.load()));
  } else if(s=="observe") {
    const uint32_t until=millis()+3000;while(!seen.load() && int32_t(until-millis())>0)delay(1);
    Serial.printf("RESET_BEGIN seen=%u before=%u mounted=%u jobs=%u parked=%u\r\n",seen.load(),beforeMounted.load(),afterMounted.load(),afterReady.load(),unsigned(parked.load()));
    Serial.printf("RESET_BLOCKED open=%u data=%u control=%u\r\n",openBlocked.load(),dataBlocked.load(),controlBlocked.load());
    monitor=false;released=true;
  } else if(s=="fresh") {
    const uint32_t until=millis()+3000;while(oldReturns.load()<2 && int32_t(until-millis())>0)delay(1);
    auto tx=vendor.writeDirect(newTx,sizeof(newTx));newTxId=tx.id;
    auto rx=vendor.readDirect(newRx,sizeof(newRx));newRxId=rx.id;
    Serial.printf("FRESH %u old=%u bad=%u\r\n",unsigned(bool(tx) && bool(rx)),oldReturns.load(),bad.load());
  } else if(s=="result") {
    const uint32_t until=millis()+3000;while(successes.load()<2 && int32_t(until-millis())>0)delay(1);
    Serial.printf("RESULT old=%u success=%u bad=%u\r\n",oldReturns.load(),successes.load(),bad.load());
  } else if(s=="release") {monitor=false;released=true;Serial.println("RELEASED");}
  else if(s=="stop") {monitor=false;released=true;Serial.println(device.end()==ESP_OK ? "STOPPED":"STOP_FAILED");}
}
