#include <Arduino.h>
#include <EspUsbDevice.h>
#include "../../../../src/internal/EspUsbController.h"
#include "../../../../src/internal/EspUsbDeviceEndpoint.h"
#include "../../../../src/internal/EspUsbRuntime.h"
#include "../../../../src/internal/EspUsbDwc2Hardware.h"
#include <atomic>
#include "../../common/probe_link.h"
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
alignas(64) uint8_t oldTx[64],oldRx[PROBE_BULK_MPS],newTx[64],newRx[PROBE_BULK_MPS];
std::atomic<bool> irqMasked{false};
std::atomic<unsigned> setups{0},oldReturns{0},successes{0},bad{0};
std::atomic<bool> parked{false},released{false},armed{false},newTxPending{false},newRxPending{false};
uint64_t oldTxId=0,oldRxId=0,newTxId=0,newRxId=0;
unsigned setupMark=0;uint8_t input=0,output=0;
extern "C" void espusb_device_event_hook(uint8_t,uint32_t event,bool) {if(event==ESPUSB_CONTROLLER_EVENT_SETUP_RECEIVED)++setups;}
void park(void *) {
  parked=true;uint32_t until=millis()+10000;
  while(!released.load() && int32_t(until-millis())>0)delay(1);
  if(!released.load())++bad;parked=false;
}
espusb::internal::UsbRuntimeJob parkJob{park,nullptr};
void armNew(void *) {
  newTxPending=newRxPending=true;
  auto tx=vendor.writeDirect(newTx,64);newTxId=tx.id;
  auto rx=vendor.readDirect(newRx,sizeof(newRx));newRxId=rx.id;
  if(!tx || !rx)++bad;
  armed=true;
}
void setup() {
  Serial.begin(115200);Serial.setTimeout(100);
  memset(oldTx,0xdd,64);memset(oldRx,0xcc,sizeof(oldRx));
  for(unsigned i=0;i<64;++i)newTx[i]=uint8_t(i*37+13);
  vendor.onDirectTxComplete([](const EspUsbTransferResult &r) {
    if(r.buffer==oldTx){if(r.id!=oldTxId || r.status!=EspUsbTransferStatus::BusReset)++bad;++oldReturns;}
    else if(r.buffer==newTx){if(!newTxPending.exchange(false) || r.id!=newTxId || r.status!=EspUsbTransferStatus::Success || !r.actualBytesKnown || r.actualBytes!=64)++bad;++successes;}
    else ++bad;
  });
  vendor.onDirectRxComplete([](const EspUsbTransferResult &r) {
    if(r.buffer==oldRx){if(r.id!=oldRxId || r.status!=EspUsbTransferStatus::BusReset)++bad;++oldReturns;}
    else if(r.buffer==newRx){
      if(!newRxPending.exchange(false) || r.id!=newRxId || r.status!=EspUsbTransferStatus::Success || !r.actualBytesKnown || r.actualBytes!=64)++bad;
      for(unsigned i=0;i<64;++i)if(newRx[i]!=uint8_t(i*17+7))++bad;++successes;
    } else ++bad;
  });
}
void loop() {
  if(!Serial.available()){delay(1);return;}String s=Serial.readStringUntil('\n');s.trim();
  if(s.startsWith("start ")) {
    EspUsbBulkStreamConfig cfg;cfg.rxMode=EspUsbBulkRxMode::Borrowed;cfg.rxCapacity=cfg.txCapacity=cfg.transferBytes=0;
    EspUsbDeviceConfig identity;identity.eventQueueDepth=s.substring(6).toInt();
    bool ok=vendor.configure(cfg)==ESP_OK && device.begin(identity);
    const auto *d=device.configurationDescriptor(0);const unsigned n=d ? d[2]|unsigned(d[3])<<8:0;
    for(unsigned j=9;j+2<=n && d[j]>=2 && d[j]<=n-j;j+=d[j])if(d[j+1]==5)(d[j+2]&128 ? input:output)=d[j+2];
    Serial.printf("STARTED %u\r\n",unsigned(ok && input && output));
  } else if(s=="prime") {
    oldReturns=successes=bad=0;armed=newTxPending=newRxPending=false;memset(newRx,0x5a,sizeof(newRx));
    auto tx=vendor.writeDirect(oldTx,64);oldTxId=tx.id;auto rx=vendor.readDirect(oldRx,sizeof(oldRx));oldRxId=rx.id;
    Serial.printf("PRIMED %u\r\n",unsigned(bool(tx) && bool(rx)));
  } else if(s=="park") {
    released=false;setupMark=setups.load();bool ok=espusb::internal::postUsbRuntimeJob(parkJob);
    uint32_t until=millis()+3000;while(ok && !parked.load() && int32_t(until-millis())>0)delay(1);
    Serial.printf("PARKED %u\r\n",unsigned(ok && parked.load()));
  } else if(s=="mask") {
    uint32_t until=millis()+3000;while(setups.load()==setupMark && int32_t(until-millis())>0)delay(1);
    bool ok=parked.load() && setups.load()>setupMark;
    if(ok){espusb_controller_int_disable(espusb::internal::usbRuntimeRhport());irqMasked=true;}
    Serial.printf("MASKED %u\r\n",unsigned(ok));
  } else if(s=="latched") {
    EspUsbDwc2Diepint status={};if(input&15)status.value=espusb_dwc2_registers(espusb::internal::usbRuntimeRhport())->epin[input&15].diepint;
    const bool completed=status.xfer_complete;
    Serial.printf("LATCHED %u\r\n",unsigned(irqMasked.load() && completed && !oldReturns.load()));
  } else if(s=="inject_latched") {
    bool ok=irqMasked.load() && parked.load() && espusb_device_try_defer(armNew,nullptr);released=true;
    uint32_t until=millis()+3000;while(!armed.load() && int32_t(until-millis())>0)delay(1);
    ok &= armed.load();
    if(irqMasked.exchange(false))espusb_controller_int_enable(espusb::internal::usbRuntimeRhport());
    Serial.printf("INJECTED %u\r\n",unsigned(ok));
  } else if(s=="inject") {
    uint32_t until=millis()+3000;while(setups.load()==setupMark && int32_t(until-millis())>0)delay(1);
    bool ok=parked.load() && setups.load()>setupMark && espusb_device_try_defer(armNew,nullptr);
    if(ok)for(uint8_t ep:{input,output})espusb_controller_event_xfer_complete(espusb::internal::usbRuntimeRhport(),ep,64,ESPUSB_TRANSFER_SUCCESS,false);
    released=true;Serial.printf("INJECTED %u\r\n",unsigned(ok));
  } else if(s=="pending" || s=="result") {
    uint32_t until=millis()+3000;
    while((!armed.load() || (s=="result" && successes.load()<2)) && int32_t(until-millis())>0)delay(1);
    if(s=="pending")delay(20);
    Serial.printf("STATE old=%u success=%u tx=%u rx=%u bad=%u\r\n",oldReturns.load(),successes.load(),unsigned(newTxPending.load()),unsigned(newRxPending.load()),bad.load());
  } else if(s=="release") {
    if(irqMasked.exchange(false))espusb_controller_int_enable(espusb::internal::usbRuntimeRhport());
    released=true;Serial.println("RELEASED");
  } else if(s=="stop") {
    released=true;Serial.println(device.end()==ESP_OK ? "STOPPED":"STOP_FAILED");
  }
}
