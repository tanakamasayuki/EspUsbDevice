#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include "../../../../src/internal/EspUsbDeviceEndpoint.h"
#include "../../../../src/internal/EspUsbController.h"
#include "../../../../src/internal/EspUsbDwc2Runtime.h"
#include "../../../../src/internal/EspUsbTinyUsbRuntime.h"
#include <atomic>
#include "soc/usb_dwc_struct.h"
// S3 has no cache on internal DMA RAM. Replace its no-op backend hooks to
// verify synchronization failure cleanup without adding product test switches.
unsigned cacheFailure=0,cacheClean=0,cacheInvalidate=0,cacheBad=0;
extern "C" bool espusb_controller_dcache_clean(const void *address,uint32_t bytes) {
  if(cacheFailure){++cacheClean;if((uintptr_t(address)&63) || bytes!=64 || espusb_dwc2_runtime_bytes())++cacheBad;}
  return cacheFailure!=1;
}
extern "C" bool espusb_controller_dcache_invalidate(const void *address,uint32_t bytes) {
  if(cacheFailure){++cacheInvalidate;if((uintptr_t(address)&63) || bytes!=64 || espusb_dwc2_runtime_bytes())++cacheBad;}
  return cacheFailure!=2;
}
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
uint8_t controlIn[129], controlOut[129];
alignas(64) uint8_t direct[64];
std::atomic<unsigned> inputs{0},outputs{0},empty{0},bad{0},successes{0},resets{0},stops{0};
uint64_t ticket=0;
void noop(void *) {}
void overflow(void *) {
  while(espusb_device_try_defer(noop,nullptr)) {}
  // Fill the hardware reserve, then exceed physical queue capacity.
  for(unsigned i=0;i<2;++i)espusb_controller_event_bus_signal(espusb::internal::tinyUsbRuntimeRhport(),ESPUSB_CONTROLLER_EVENT_RESUME,false);
}
espusb::internal::TinyUsbRuntimeJob overflowJob{overflow,nullptr};
bool selectedStorage() {
  bool ok=true;size_t minimal=0,paired=0,sparse=0;
  {
    EspUsbDevice local;ok &= local.begin() && !espusb_dwc2_runtime_bytes() && !espusb_dwc2_runtime_transfers();
    ok &= local.end()==ESP_OK;delay(10);
  }
  {
    EspUsbDevice local;EspUsbDeviceDfu control(local,EspUsbDeviceDfuMode::Runtime);
    ok &= local.begin();
    minimal=espusb_dwc2_runtime_bytes();
    ok &= espusb_dwc2_runtime_transfers()==2 && minimal>0;
    ok &= local.end()==ESP_OK && !espusb_dwc2_runtime_bytes() && !espusb_dwc2_runtime_transfers();delay(10);
  }
  {
    const uint8_t report[]={0x06,0,0xff,0x09,1,0xa1,1,0x75,8,0x95,1,0x09,1,0x81,2,0xc0};
    EspUsbDevice local;EspUsbDeviceHidCustom input(local,report,sizeof(report),1);
    ok &= local.begin() && espusb_dwc2_runtime_transfers()==3;
    ok &= local.end()==ESP_OK && !espusb_dwc2_runtime_bytes();delay(10);
  }
  {
    EspUsbDevice local;EspUsbDeviceVendor stream(local);
    ok &= local.begin() && espusb_dwc2_runtime_transfers()==4;
    paired=espusb_dwc2_runtime_bytes();ok &= paired>minimal;
    ok &= local.end()==ESP_OK && !espusb_dwc2_runtime_bytes();delay(10);
  }
  {
    EspUsbDevice local;EspUsbDeviceCcid first(local),second(local);
    // Sparse OUT addresses 1/3; IN 1/2/3/4. Add both EP0 directions.
    ok &= local.begin() && espusb_dwc2_runtime_transfers()==8 && espusb_dwc2_runtime_bytes()>paired;
    sparse=espusb_dwc2_runtime_bytes();
    ok &= local.end()==ESP_OK && !espusb_dwc2_runtime_bytes();delay(10);
  }
  Serial.printf("DWC2_STORAGE ep0=%u vendor=%u sparse=%u\r\n",unsigned(minimal),unsigned(paired),unsigned(sparse));
  return ok;
}
bool cacheFailures() {
  bool ok=true;
  for(unsigned failure:{1u,2u}) {
    cacheFailure=failure;cacheClean=cacheInvalidate=cacheBad=0;
    ok &= !device.begin() && device.lastError()==ESP_FAIL && !espusb_dwc2_runtime_bytes() && device.end()==ESP_OK;
    ok &= cacheClean==1 && cacheInvalidate==(failure==1 ? 0u:1u) && !cacheBad;
    cacheFailure=0;
    ok &= device.begin() && espusb_dwc2_runtime_transfers()==4 && device.end()==ESP_OK && !espusb_dwc2_runtime_bytes();
    delay(10);
  }
  return ok;
}
bool vbusPolicy() {
  bool ok=true;
  for(bool sensing:{false,true,false}) {
    EspUsbDeviceConfig config;config.vbusSensing=sensing;
    ok &= device.begin(config);
    usb_dwc_gotgctl_reg_t status={};status.val=USB_DWC.gotgctl_reg.val;
    ok &= bool(status.bvalidoven)==!sensing;
    if(!sensing)ok &= bool(status.bvalidovval);
    ok &= device.end()==ESP_OK;
    delay(10);
  }
  Serial.printf("VBUS_POLICY %u\r\n",unsigned(ok));
  return ok;
}
bool metadata() {
  EspUsbDeviceConfig cfg;
  bool ok=selectedStorage() && cacheFailures() && vbusPolicy();
  for (unsigned n:{0u}) {
    cfg.eventQueueDepth=n;
    ok &= !device.begin(cfg) && device.lastError()==ESP_ERR_INVALID_SIZE && device.end()==ESP_OK;
  }
  for (unsigned n:{1u,4u,16u,31u}) {
    cfg.eventQueueDepth=n;
    ok &= device.begin(cfg) && espusb_device_queue_capacity()==n && espusb_dwc2_runtime_transfers()==4 && device.end()==ESP_OK && !espusb_device_queue_capacity() && !espusb_dwc2_runtime_bytes();
    delay(10); // FreeRTOS idle task reclaims self-deleted worker TCBs.
  }
  return ok;
}
void setup() {
  Serial.begin(115200);Serial.setTimeout(100);
  for(unsigned i=0;i<sizeof(controlIn);++i)controlIn[i]=uint8_t(i*37+13);
  for(unsigned i=0;i<sizeof(direct);++i)direct[i]=uint8_t(i*17+7);
  vendor.onControlRequest([](const EspUsbDeviceVendorControlRequest &r) {
    if (r.bRequest==0x60 && r.bmRequestType==0xc0 && r.wValue<=sizeof(controlIn)) {
      const uint16_t actual=r.stage==ESP_USB_DEVICE_CONTROL_STAGE_SETUP ? 0 : std::min(r.wValue,r.wLength);
      if(r.actualBytes!=actual)++bad;
      if(r.stage==ESP_USB_DEVICE_CONTROL_STAGE_SETUP)return vendor.sendControlResponse(r,controlIn,r.wValue);
      if(r.stage==ESP_USB_DEVICE_CONTROL_STAGE_ACK)++inputs;
      return true;
    }
    if(r.bRequest==0x61 && r.bmRequestType==0x40 && r.wLength && r.wLength<=sizeof(controlOut)) {
      if(r.actualBytes!=(r.stage==ESP_USB_DEVICE_CONTROL_STAGE_SETUP ? 0:r.wLength))++bad;
      if(r.stage==ESP_USB_DEVICE_CONTROL_STAGE_SETUP)return vendor.sendControlResponse(r,controlOut,r.wLength);
      if(r.stage==ESP_USB_DEVICE_CONTROL_STAGE_DATA) {
        if(r.actualBytes!=r.wLength)return false;
        for(unsigned i=0;i<r.wLength;++i)if(controlOut[i]!=uint8_t(i*37+13))++bad;
      }
      if(r.stage==ESP_USB_DEVICE_CONTROL_STAGE_ACK)++outputs;
      return true;
    }
    if(r.bRequest==0x62 && !r.wLength && (r.bmRequestType==0x40 || r.bmRequestType==0xc0)) {
      if(r.actualBytes)++bad;
      if(r.stage==ESP_USB_DEVICE_CONTROL_STAGE_SETUP)return vendor.sendControlResponse(r);
      if(r.stage==ESP_USB_DEVICE_CONTROL_STAGE_ACK)++empty;
      return true;
    }
    return false;
  });
  vendor.onDirectTxComplete([](const EspUsbTransferResult &r){
    if(r.id!=ticket || r.buffer!=direct || r.requestedBytes!=64)++bad;
    if(r.status==EspUsbTransferStatus::Success){++successes;if(!r.actualBytesKnown || r.actualBytes!=64)++bad;}
    else if(r.status==EspUsbTransferStatus::BusReset)++resets;
    else if(r.status==EspUsbTransferStatus::Stopped)++stops;
    else ++bad;
  });
}
void loop() {
  if(!Serial.available()){delay(1);return;}String c=Serial.readStringUntil('\n');c.trim();
  if(c=="metadata") {
    bool ok=metadata();const size_t before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    for(unsigned i=0;i<3 && ok;++i)ok &= metadata();
    Serial.printf("METADATA ok=%u before=%u after=%u\r\n",unsigned(ok),unsigned(before),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  } else if(c.startsWith("start ")) {
    inputs=outputs=empty=bad=successes=resets=stops=0;
    EspUsbDeviceConfig cfg;cfg.selfPowered=true;cfg.eventQueueDepth=c.substring(6).toInt();
    bool ok=device.begin(cfg) && espusb_device_queue_capacity()==cfg.eventQueueDepth;
    Serial.printf("STARTED %u\r\n",unsigned(ok));
  } else if(c=="queue") {
    auto r=vendor.writeDirect(direct,64,EspUsbTransferTermination::EndWithShortPacket);ticket=r.id;
    Serial.printf("QUEUED %u\r\n",unsigned(bool(r)));
  } else if(c=="overflow") {
    const unsigned before=resets.load();
    bool ok=espusb::internal::postTinyUsbRuntimeJob(overflowJob);
    uint32_t until=millis()+3000;
    while(resets.load()==before && int32_t(until-millis())>0)delay(1);
    ok &= resets.load()==before+1 && !device.ready() && !bad.load();
    Serial.printf("OVERFLOW %u\r\n",unsigned(ok));
  } else if(c=="stats") {
    Serial.printf("STATS input=%u output=%u empty=%u success=%u reset=%u stop=%u bad=%u\r\n",inputs.load(),outputs.load(),empty.load(),successes.load(),resets.load(),stops.load(),bad.load());
  } else if(c=="stop")Serial.println(device.end()==ESP_OK && heap_caps_check_integrity_all(true) ? "STOPPED" : "STOP_FAILED");
}
