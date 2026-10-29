#include <Arduino.h>
#include <EspUsbDevice.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"

EspUsbDevice device;
EspUsbDeviceMsc disk(device);
EspUsbDeviceVendor vendor(device);
EspUsbHost host;
EspUsbDeviceMscRamDisk *ram=nullptr;
uint8_t *medium=nullptr,*rxRing=nullptr,*txRing=nullptr;
uint8_t sent[4097],received[4097];
std::atomic<unsigned> address{0},speed{255},bad{0};
std::atomic<size_t> count{0};
std::atomic<size_t> wanted{0};
bool hostStarted=false;
constexpr size_t diskBytes=64*512,ringBytes=8192;

bool guard(const uint8_t *p,size_t capacity) {
  if(!p) return false;
  for(size_t i=0;i<64;++i) if(p[i]!=0xc3 || p[64+capacity+i]!=0xc3) return false;
  return true;
}
bool guards() {return guard(medium,diskBytes)&&guard(rxRing,ringBytes)&&guard(txRing,ringBytes);}
bool stopUsb() {
  if(hostStarted && address.load() && host.vendorReadQueueReady(address.load())) host.vendorReadQueueEnd(address.load());
  bool ok=device.end()==ESP_OK;
  if(hostStarted) host.end();
  hostStarted=false;address.store(0);
  if(medium || rxRing || txRing) ok &= guards();
  delete ram;ram=nullptr;
  heap_caps_free(medium);heap_caps_free(rxRing);heap_caps_free(txRing);
  medium=rxRing=txRing=nullptr;
  return ok && heap_caps_check_integrity_all(true);
}
void setup() {
  Serial.begin(115200);Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &i) {
    if(strcmp(i.product,"v3 P4 PSRAM")==0) {speed.store(i.speed);address.store(i.address);}
  });
  host.onVendorData([](const EspUsbHostVendorData &event) {
    if(!event.length) return;
    const size_t offset=count.load();
    if(event.data && offset+event.length<=wanted.load() && offset+event.length<=sizeof(received)) memcpy(received+offset,event.data,event.length);
    else ++bad;
    count.store(offset+event.length,std::memory_order_release);
  });
  vendor.onRx([](size_t) {
    uint8_t data[64];
    while(vendor.available() && vendor.writeAvailable()) {
      size_t n=vendor.read(data,min(sizeof(data),vendor.writeAvailable()));
      if(!n) break;
      if(vendor.write(data,n)!=n) ++bad;
    }
  });
}
void loop() {
  if(!Serial.available()) {delay(1);return;}
  String command=Serial.readStringUntil('\n');command.trim();
  if(command=="start hs" || command=="start fs") {
    const bool hs=command.endsWith("hs");
    Serial.printf("MEMORY revision=%u cpu=%u psram=%u heap=%u\r\n",unsigned(ESP.getChipRevision()),unsigned(ESP.getCpuFreqMHz()),unsigned(ESP.getPsramSize()),unsigned(heap_caps_get_total_size(MALLOC_CAP_SPIRAM)));
    address.store(0);speed.store(255);bad.store(0);
    medium=static_cast<uint8_t*>(heap_caps_aligned_alloc(64,diskBytes+128,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    rxRing=static_cast<uint8_t*>(heap_caps_aligned_alloc(64,ringBytes+128,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    txRing=static_cast<uint8_t*>(heap_caps_aligned_alloc(64,ringBytes+128,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    bool ok=medium && rxRing && txRing;
    if(ok) {
      memset(medium,0xc3,diskBytes+128);memset(rxRing,0xc3,ringBytes+128);memset(txRing,0xc3,ringBytes+128);
      ok &= esp_ptr_external_ram(medium)&&esp_ptr_external_ram(rxRing)&&esp_ptr_external_ram(txRing);
      ram=new EspUsbDeviceMscRamDisk(medium+64,64,512);ram->clear(0x5a);ok &= ram->attach(disk);
      EspUsbMscConfig mc;mc.transferBytes=512;ok &= disk.configure(mc)==ESP_OK;disk.mediaPresent(true);disk.isWritable(true);
      EspUsbVendorConfig vc;vc.rxCapacity=vc.txCapacity=ringBytes;vc.rxBuffer=rxRing+64;vc.txBuffer=txRing+64;vc.transferBytes=64;
      ok &= vendor.configure(vc)==ESP_OK;
      EspUsbHostConfig hc;hc.port=hs?ESP_USB_HOST_PORT_FULL_SPEED:ESP_USB_HOST_PORT_HIGH_SPEED;
      hostStarted=host.begin(hc);ok &= hostStarted;
      EspUsbDeviceConfig dc;dc.product="v3 P4 PSRAM";dc.controller=hs?EspUsbController::HighSpeed:EspUsbController::FullSpeed;ok &= device.begin(dc);
      const uint32_t deadline=millis()+12000;
      while(ok && (!address.load() || !device.ready() || !host.mscReady(address.load())) && int32_t(deadline-millis())>0) delay(1);
      ok &= address.load() && device.ready() && host.mscReady(address.load());
      if(ok) ok &= host.vendorOpen(address.load(),0xff,ESP_USB_HOST_VENDOR_READ_CONTINUOUS,64) && host.vendorReadQueueBegin(1,64,address.load());
    }
    Serial.printf("START ok=%u controller=%s speed=%u external=%u\r\n",unsigned(ok),hs?"hs":"fs",speed.load(),unsigned(medium&&rxRing&&txRing&&esp_ptr_external_ram(medium)&&esp_ptr_external_ram(rxRing)&&esp_ptr_external_ram(txRing)));
  } else if(command=="exercise") {
    bool ok=ram && device.ready();unsigned blocks=0,echoes=0;
    for(unsigned lba:{0u,3u,57u}) {
      if(!ok) break;
      for(size_t i=0;i<7*512;++i) sent[i]=uint8_t(i*17+lba);
      ok &= host.mscWriteBlocks(lba,sent,7,address.load()) && host.mscReadBlocks(lba,received,7,address.load()) && !memcmp(sent,received,7*512) && !memcmp(ram->data()+lba*512,sent,7*512);
      if(ok) blocks+=7;
    }
    const size_t lengths[]={63,64,65,513,4097};
    for(unsigned repeat=0;repeat<8 && ok;++repeat) for(size_t n:lengths) {
      if(!ok) break;
      for(size_t i=0;i<n;++i) sent[i]=uint8_t(i*19+n+repeat);
      memset(received,0,sizeof(received));count.store(0);wanted.store(n);
      ok &= host.vendorWrite(sent,n,address.load());
      const uint32_t deadline=millis()+5000;
      while(ok && count.load(std::memory_order_acquire)<n && int32_t(deadline-millis())>0) delay(1);
      ok &= count.load(std::memory_order_acquire)==n && !memcmp(sent,received,n) && bad.load()==0;
      if(ok) ++echoes;
    }
    ok &= guards() && heap_caps_check_integrity_all(true);
    Serial.printf("PSRAM ok=%u blocks=%u echoes=%u bad=%u guards=%u\r\n",unsigned(ok),blocks,echoes,bad.load(),unsigned(guards()));
  } else if(command=="stop") Serial.println(stopUsb()?"STOPPED":"STOP_FAILED");
}
