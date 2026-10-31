#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include "../../../../src/internal/EspUsbDwc2Platform.h"
#include "../../../../src/internal/EspUsbDwc2Runtime.h"
#include "../../../../src/internal/EspUsbRuntime.h"
#include "../../common/probe_link.h"
// Non-control IN endpoints: 4 on S3, 7 on the P4 HS port (EspUsbDwc2Platform.cpp).
// One extra CDC+MSC (3 IN) on HS keeps every mode the same count over the limit.
constexpr unsigned EXTRA=PROBE_BULK_MPS==64 ? 0:1;
unsigned irqCalls=0;
extern "C" esp_err_t espusb_dwc2_allocate_interrupt(int source,int flags,intr_handler_t handler,void *context,intr_handle_t *result) {
  ++irqCalls;return esp_intr_alloc(source,flags,handler,context,result);
}
EspUsbDevice device;
EspUsbDeviceCdcSerial *serialPort[3+EXTRA]={};
EspUsbDeviceMsc *disk[2+EXTRA]={};
void setup(){Serial.begin(115200);Serial.setTimeout(100);}
void loop() {
  for(auto *p:serialPort)if(p){uint8_t b[64];size_t n=p->read(b,std::min(sizeof(b),p->writeAvailable()));if(n)p->write(b,n);}
  if(!Serial.available()){delay(1);return;}
  String s=Serial.readStringUntil('\n');s.trim();
  if(s.startsWith("attempt ")) {
    unsigned mode=s.substring(8).toInt();
    if(device.started() || mode>2){Serial.println("INVALID_ATTEMPT");return;}
    for(auto *&p:serialPort){delete p;p=nullptr;}for(auto *&p:disk){delete p;p=nullptr;}
    bool settings=true;
    for(unsigned i=0;i<(mode==1 ? 3u:2u)+EXTRA;++i) {
      serialPort[i]=new EspUsbDeviceCdcSerial(device);
      EspUsbBulkStreamConfig c;c.rxCapacity=97;c.txCapacity=131;c.transferBytes=64;
      settings &= serialPort[i]->configure(c)==ESP_OK;
    }
    for(unsigned i=0;i<(mode==1 ? 0u:mode==2 ? 2u:1u)+EXTRA;++i) {
      disk[i]=new EspUsbDeviceMsc(device);
      disk[i]->onRead([](uint32_t,uint32_t,void *b,uint32_t n){memset(b,0,n);return int32_t(n);});
      disk[i]->mediaPresent(true);EspUsbMscConfig c;c.transferBytes=PROBE_BULK_MPS;
      settings &= disk[i]->configure(c)==ESP_OK && disk[i]->begin(8,512);
    }
    const unsigned metaIrq=irqCalls;EspUsbDeviceConfig meta;meta.startMode=EspUsbDeviceStartMode::DescriptorsOnly;
    const bool metaStarted=device.begin(meta);
    bool metadata=settings && !metaStarted && device.lastError()==ESP_ERR_INVALID_SIZE && !device.started() && irqCalls==metaIrq;
    metadata &= device.end()==ESP_OK;
    delay(20);const size_t before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);const unsigned irqBefore=irqCalls;
    unsigned rejected=0;esp_err_t error=ESP_OK;bool released=true;
    for(unsigned i=0;i<3;++i) {
      const bool started=device.begin();error=device.lastError();
      if(!started && error==ESP_ERR_INVALID_SIZE)++rejected;
      if(started)device.end();
      released &= !device.started() && !espusb::internal::usbRuntimeStarted() && !espusb_dwc2_runtime_bytes();
      for(auto *p:serialPort)if(p)released &= !p->readCapacity() && !p->writeCapacity();
      for(auto *p:disk)if(p)released &= !p->transferCapacity();
    }
    delay(20);const size_t after=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    Serial.printf("LIMITS mode=%u rejected=%u error=%d irq=%u released=%u before=%u after=%u metaRejected=%u\r\n",mode,rejected,int(error),irqCalls-irqBefore,unsigned(released),unsigned(before),unsigned(after),unsigned(metadata));
  } else if(s=="recover") {
    for(unsigned i=2;i<3+EXTRA;++i){delete serialPort[i];serialPort[i]=nullptr;}for(auto *&p:disk){delete p;p=nullptr;}
    EspUsbDeviceConfig c;c.product="v3 controller capacity";
    Serial.printf("RECOVERED %u\r\n",unsigned(device.begin(c)));
  } else if(s=="stop")Serial.println(device.end()==ESP_OK ? "STOPPED":"STOP_FAILED");
}
