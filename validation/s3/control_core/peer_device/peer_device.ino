#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include "../../../../src/device/usbd_pvt.h"
#include "../../../../src/device/dcd.h"
#include "../../../../src/internal/EspUsbTinyUsbRuntime.h"
#include <atomic>
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
uint8_t controlIn[129], controlOut[129];
alignas(64) uint8_t direct[64];
std::atomic<unsigned> inputs{0},outputs{0},empty{0},bad{0},successes{0},resets{0},stops{0};
uint64_t ticket=0;
void noop(void *) {}
void overflow(void *) {
  while(usbd_try_defer_func(noop,nullptr)) {}
  // Fill the hardware reserve, then exceed physical queue capacity.
  for(unsigned i=0;i<2;++i)dcd_event_bus_signal(espusb::internal::tinyUsbRuntimeRhport(),DCD_EVENT_RESUME,false);
}
espusb::internal::TinyUsbRuntimeJob overflowJob{overflow,nullptr};
bool metadata() {
  EspUsbDeviceConfig cfg;
  bool ok=true;
  for (unsigned n:{0u}) {
    cfg.eventQueueDepth=n;
    ok &= !device.begin(cfg) && device.lastError()==ESP_ERR_INVALID_SIZE && device.end()==ESP_OK;
  }
  for (unsigned n:{1u,4u,16u,31u}) {
    cfg.eventQueueDepth=n;
    ok &= device.begin(cfg) && usbd_event_queue_capacity()==n && device.end()==ESP_OK && !usbd_event_queue_capacity();
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
    bool ok=device.begin(cfg) && usbd_event_queue_capacity()==cfg.eventQueueDepth;
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
