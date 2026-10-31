#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include "../../../../src/internal/EspUsbDeviceEndpoint.h"
#include "../../../../src/internal/EspUsbController.h"
#include "../../../../src/internal/EspUsbDwc2Runtime.h"
#include "../../../../src/internal/EspUsbRuntime.h"
#include <atomic>
#include "../../../../src/internal/EspUsbDwc2Timing.h"
#include "../../../../src/internal/EspUsbDwc2Platform.h"
#include "soc/usb_dwc_struct.h"
#include "soc/soc_caps.h"
#include "../../../../src/internal/EspUsbDwc2Hardware.h"
#include "../../common/probe_link.h"
static_assert(offsetof(EspUsbDwc2Registers, gotgctl)==offsetof(usb_dwc_dev_t, gotgctl_reg));
static_assert(offsetof(EspUsbDwc2Registers, dcfg)==offsetof(usb_dwc_dev_t, dcfg_reg));
static_assert(offsetof(EspUsbDwc2Registers, epin)==offsetof(usb_dwc_dev_t, diepctl0_reg));
static_assert(offsetof(EspUsbDwc2Registers, epout)==offsetof(usb_dwc_dev_t, doepctl0_reg));
static_assert(offsetof(EspUsbDwc2Registers, pcgcctl)==offsetof(usb_dwc_dev_t, pcgcctl_reg));
bool failIrq=false;unsigned irqAttempts=0;
extern "C" esp_err_t espusb_dwc2_allocate_interrupt(int source,int flags,intr_handler_t handler,
    void *context,intr_handle_t *result) {
  ++irqAttempts;
  if(failIrq){*result=nullptr;return ESP_ERR_NO_MEM;}
  return esp_intr_alloc(source,flags,handler,context,result);
}
// S3 has no cache on internal DMA RAM. Replace its no-op backend hooks to
// verify synchronization failure cleanup without adding product test switches.
// The P4 hooks are the real (strong) cache backends, so this check is S3-only.
unsigned cacheFailure=0,cacheClean=0,cacheInvalidate=0,cacheBad=0;
#if !SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
extern "C" bool espusb_controller_dcache_clean(const void *address,uint32_t bytes) {
  if(cacheFailure){++cacheClean;if((uintptr_t(address)&63) || bytes!=64 || espusb_dwc2_runtime_bytes())++cacheBad;}
  return cacheFailure!=1;
}
extern "C" bool espusb_controller_dcache_invalidate(const void *address,uint32_t bytes) {
  if(cacheFailure){++cacheInvalidate;if((uintptr_t(address)&63) || bytes!=64 || espusb_dwc2_runtime_bytes())++cacheBad;}
  return cacheFailure!=2;
}
#endif
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
uint8_t controlIn[129], controlOut[129];
alignas(64) uint8_t direct[PROBE_BULK_MPS];
std::atomic<unsigned> inputs{0},outputs{0},empty{0},bad{0},successes{0},resets{0},stops{0};
uint64_t ticket=0;
void noop(void *) {}
void overflow(void *) {
  while(espusb_device_try_defer(noop,nullptr)) {}
  // Fill the hardware reserve, then exceed physical queue capacity.
  for(unsigned i=0;i<2;++i)espusb_controller_event_bus_signal(espusb::internal::usbRuntimeRhport(),ESPUSB_CONTROLLER_EVENT_RESUME,false);
}
espusb::internal::UsbRuntimeJob overflowJob{overflow,nullptr};
bool selectedStorage() {
  bool ok=true;size_t minimal=0,paired=0,mixed=0,sparse=0;
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
    const uint8_t report[]={0x06,0,0xff,0x09,1,0xa1,1,0x75,8,0x95,1,0x09,1,0x81,2,0xc0};
    EspUsbDevice local;EspUsbDeviceCdcSerial serial(local);
    EspUsbDeviceHidCustom input(local,report,sizeof(report),1);
    ok &= local.begin() && espusb_dwc2_runtime_transfers()==6;
    mixed=espusb_dwc2_runtime_bytes();ok &= mixed>=paired;
    ok &= local.end()==ESP_OK && !espusb_dwc2_runtime_bytes();delay(10);
  }
  {
    EspUsbDevice local;EspUsbDeviceCcid first(local),second(local);
    // CCID requires messageBytes >= bulk MPS; the 320-byte default is FS-sized.
    EspUsbCcidConfig message;if(message.messageBytes<PROBE_BULK_MPS)message.messageBytes=PROBE_BULK_MPS;
    ok &= first.configure(message)==ESP_OK && second.configure(message)==ESP_OK;
    // Sparse OUT addresses 1/3; IN 1/2/3/4. Add both EP0 directions.
    ok &= local.begin() && espusb_dwc2_runtime_transfers()==8 && espusb_dwc2_runtime_bytes()>paired;
    sparse=espusb_dwc2_runtime_bytes();
    ok &= local.end()==ESP_OK && !espusb_dwc2_runtime_bytes();delay(10);
  }
  Serial.printf("DWC2_STORAGE ep0=%u vendor=%u mixed=%u sparse=%u\r\n",unsigned(minimal),unsigned(paired),unsigned(mixed),unsigned(sparse));
  return ok;
}
bool cacheFailures() {
  bool ok=true;
#if !SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
  for(unsigned failure:{1u,2u}) {
    cacheFailure=failure;cacheClean=cacheInvalidate=cacheBad=0;
    ok &= !device.begin() && device.lastError()==ESP_FAIL && !espusb_dwc2_runtime_bytes() && device.end()==ESP_OK;
    ok &= cacheClean==1 && cacheInvalidate==(failure==1 ? 0u:1u) && !cacheBad;
    cacheFailure=0;
    ok &= device.begin() && espusb_dwc2_runtime_transfers()==4 && device.end()==ESP_OK && !espusb_dwc2_runtime_bytes();
    delay(10);
  }
#endif
  return ok;
}
bool vbusPolicy() {
  bool ok=true;
  for(bool sensing:{false,true,false}) {
    EspUsbDeviceConfig config;config.vbusSensing=sensing;
    ok &= device.begin(config);
    const auto *regs=espusb_dwc2_registers(espusb::internal::usbRuntimeRhport());
    const uint32_t status=regs ? regs->gotgctl:0;ok &= regs!=nullptr;
    ok &= bool(status&GOTGCTL_BVALOEN)==!sensing;
    if(!sensing)ok &= bool(status&GOTGCTL_BVALOVAL);
    ok &= device.end()==ESP_OK;
    delay(10);
  }
  Serial.printf("VBUS_POLICY %u\r\n",unsigned(ok));
  return ok;
}
bool irqFailures() {
  bool ok=true;
  for(unsigned attempt=0;attempt<2;++attempt) {
    failIrq=true;const unsigned before=irqAttempts;
    ok &= !device.begin() && device.lastError()==ESP_FAIL && irqAttempts==before+1;
    ok &= !espusb_dwc2_runtime_bytes() && !espusb::internal::usbRuntimeStarted() && device.end()==ESP_OK;
    failIrq=false;ok &= device.begin();
    const unsigned installed=irqAttempts;
    failIrq=true;
    for(unsigned mask=0;mask<3;++mask) {
      const auto port=espusb::internal::usbRuntimeRhport();
      espusb_controller_int_disable(port);ok &= espusb_controller_int_enable(port);
    }
    ok &= irqAttempts==installed;
    failIrq=false;ok &= device.end()==ESP_OK;
    delay(10);
  }
  Serial.printf("IRQ_RECOVERY %u\r\n",unsigned(ok));
  return ok;
}
bool intervals() {
  struct Sample {uint8_t type,encoded;bool high,valid;uint16_t ticks;};
  const Sample cases[]={
    {2,0,true,true,0},{2,255,true,true,0},{0,0,true,true,0},
    {1,1,false,true,1},{1,3,false,true,4},{1,16,false,true,32768},
    {1,9,true,true,256},{1,16,true,true,32768},{1,0,true,false,0},{1,17,false,false,0},
    {3,255,false,true,255},{3,0,false,false,0},{3,16,true,true,32768},{3,17,true,false,0},
  };
  bool ok=true;
  for(const auto &sample:cases) {
    EspUsbEndpointDescriptor endpoint={7,5,0x81,sample.type,64,sample.encoded};uint16_t ticks=0;
    const bool valid=espusb_dwc2_decode_interval(&endpoint,sample.high,&ticks);
    const bool match=valid==sample.valid && (!valid || ticks==sample.ticks);
    if(!match)Serial.printf("INTERVAL_CASE type=%u encoded=%u high=%u valid=%u ticks=%u\r\n",sample.type,sample.encoded,sample.high,valid,ticks);
    ok &= match;
  }
  ok &= device.begin();
  uint8_t address=0;
  const auto *descriptor=device.configurationDescriptor(0);
  const unsigned length=descriptor ? descriptor[2]|unsigned(descriptor[3])<<8:0;
  for(unsigned offset=9;offset+2<=length && descriptor[offset]>=2 && descriptor[offset]<=length-offset;offset+=descriptor[offset])
    if(descriptor[offset+1]==5 && (descriptor[offset+2]&128))address=descriptor[offset+2];
  ok &= address!=0;
  EspUsbEndpointDescriptor endpoint={7,5,address,1,64,0};
  const auto port=espusb::internal::usbRuntimeRhport();
  // Host has not reset/enumerated this metadata-only start. Initialize the
  // FIFO accounting explicitly before testing the low-level open operation.
  espusb_controller_edpt_close_all(port);
  for(unsigned attempt=0;attempt<32;++attempt) {
    endpoint.bInterval=0;ok &= !espusb_controller_edpt_open(port,&endpoint);
    endpoint.bInterval=17;ok &= !espusb_controller_edpt_open(port,&endpoint);
  }
  endpoint.bInterval=1;const bool opened=espusb_controller_edpt_open(port,&endpoint);ok &= opened;
  Serial.printf("INTERVAL_ENDPOINT address=%u opened=%u\r\n",unsigned(address),unsigned(opened));
  ok &= device.end()==ESP_OK;delay(10);
  Serial.printf("INTERVALS %u\r\n",unsigned(ok));return ok;
}
std::atomic<int> inspectedPriority{-1},inspectedCore{-1};
void inspectWorker(void *) {
  inspectedPriority=int(uxTaskPriorityGet(nullptr));inspectedCore=int(xPortGetCoreID());
}
espusb::internal::UsbRuntimeJob inspectJob{inspectWorker,nullptr};
bool workerSettings() {
  EspUsbDeviceConfig cfg;bool ok=true;
  const uint32_t invalidSizes[]={0,1,UINT32_MAX};
  for(uint32_t bytes:invalidSizes) {
    cfg.taskStackBytes=bytes;
    ok &= !device.begin(cfg) && device.lastError()==ESP_ERR_INVALID_SIZE && !device.started() && device.end()==ESP_OK && !espusb_dwc2_runtime_bytes();
  }
  cfg=EspUsbDeviceConfig();
  for(int value:{-2,127}) {
    cfg.taskPriority=int8_t(value);
    ok &= !device.begin(cfg) && device.lastError()==ESP_ERR_INVALID_ARG && !device.started() && device.end()==ESP_OK && !espusb_dwc2_runtime_bytes();
  }
  cfg=EspUsbDeviceConfig();
  for(int value:{-2,127}) {
    cfg.taskCoreId=int8_t(value);
    ok &= !device.begin(cfg) && device.lastError()==ESP_ERR_INVALID_ARG && !device.started() && device.end()==ESP_OK && !espusb_dwc2_runtime_bytes();
  }
  for(unsigned bytes:{4096u,8192u}) {
    cfg=EspUsbDeviceConfig();cfg.taskStackBytes=bytes;cfg.taskPriority=bytes==4096 ? 3:-1;cfg.taskCoreId=bytes==4096 ? 0:-1;
    inspectedPriority=inspectedCore=-1;
    ok &= device.begin(cfg) && device.started() && espusb::internal::postUsbRuntimeJob(inspectJob);
    const uint32_t until=millis()+2000;while(inspectedCore.load()<0 && int32_t(until-millis())>0)delay(1);
    ok &= inspectedPriority.load()==(cfg.taskPriority<0 ? configMAX_PRIORITIES-1:cfg.taskPriority) &&
        inspectedCore.load()>=0 && inspectedCore.load()<configNUMBER_OF_CORES && (cfg.taskCoreId<0 || inspectedCore.load()==cfg.taskCoreId);
    ok &= device.end()==ESP_OK && !device.started();delay(10);
  }
  Serial.printf("WORKER_SETTINGS %u\r\n",unsigned(ok));return ok;
}
bool metadata() {
  EspUsbDeviceConfig cfg;
  bool ok=selectedStorage() && cacheFailures() && vbusPolicy() && irqFailures() && intervals() && workerSettings();
  cfg.startMode=static_cast<EspUsbDeviceStartMode>(255);
  ok &= !device.begin(cfg) && device.lastError()==ESP_ERR_INVALID_ARG && !device.started() && !device.ready() && !espusb_dwc2_runtime_bytes() && device.end()==ESP_OK;
  cfg.startMode=EspUsbDeviceStartMode::DescriptorsOnly;cfg.eventQueueDepth=0;cfg.taskStackBytes=0;cfg.taskPriority=127;cfg.taskCoreId=127;
  ok &= device.begin(cfg) && device.configurationDescriptor(0) && device.started() && !device.ready() && !espusb_device_queue_capacity() && !espusb_dwc2_runtime_bytes();
  ok &= device.end()==ESP_OK;
  cfg=EspUsbDeviceConfig();
  Serial.printf("START_MODES %u\r\n",unsigned(ok));
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
    if(r.id!=ticket || r.buffer!=direct || r.requestedBytes!=sizeof(direct))++bad;
    if(r.status==EspUsbTransferStatus::Success){++successes;if(!r.actualBytesKnown || r.actualBytes!=sizeof(direct))++bad;}
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
    auto r=vendor.writeDirect(direct,sizeof(direct),EspUsbTransferTermination::EndWithShortPacket);ticket=r.id;
    Serial.printf("QUEUED %u\r\n",unsigned(bool(r)));
  } else if(c=="overflow") {
    const unsigned before=resets.load();
    bool ok=espusb::internal::postUsbRuntimeJob(overflowJob);
    uint32_t until=millis()+3000;
    while(resets.load()==before && int32_t(until-millis())>0)delay(1);
    ok &= resets.load()==before+1 && !device.ready() && !bad.load();
    Serial.printf("OVERFLOW %u\r\n",unsigned(ok));
  } else if(c=="stats") {
    Serial.printf("STATS input=%u output=%u empty=%u success=%u reset=%u stop=%u bad=%u\r\n",inputs.load(),outputs.load(),empty.load(),successes.load(),resets.load(),stops.load(),bad.load());
  } else if(c=="stop")Serial.println(device.end()==ESP_OK && heap_caps_check_integrity_all(true) ? "STOPPED" : "STOP_FAILED");
}
