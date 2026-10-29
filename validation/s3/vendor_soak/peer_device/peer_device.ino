#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <algorithm>
#include "../../../../src/internal/EspUsbController.h"
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
uint8_t rxStorage[259],txStorage[511];
uint32_t received=0,sent=0,bad=0;
size_t heapBefore=0;
std::atomic<unsigned> resets{0};
extern "C" void espusb_device_event_hook(uint8_t,uint32_t event,bool) {
  if(event==ESPUSB_CONTROLLER_EVENT_BUS_RESET)resets.fetch_add(1,std::memory_order_relaxed);
}
bool guards(){return rxStorage[0]==0xc3 && rxStorage[258]==0xc3 && txStorage[0]==0xc3 && txStorage[510]==0xc3;}
void setup(){Serial.begin(115200);Serial.setTimeout(100);}
void loop(){
  for(;;) {
    uint8_t b[64];const size_t n=vendor.read(b,std::min(sizeof(b),vendor.writeAvailable()));
    if(!n)break;received+=n;const size_t written=vendor.write(b,n);sent+=written;if(written!=n)++bad;
  }
  if(!Serial.available()){delay(1);return;}
  String s=Serial.readStringUntil('\n');s.trim();
  if(s=="start") {
    memset(rxStorage,0xc3,sizeof(rxStorage));memset(txStorage,0xc3,sizeof(txStorage));
    EspUsbBulkStreamConfig c;c.rxCapacity=257;c.txCapacity=509;c.transferBytes=128;
    c.rxBuffer=rxStorage+1;c.txBuffer=txStorage+1;c.streamTermination=EspUsbStreamTermination::Continue;
    bool ok=vendor.configure(c)==ESP_OK;
    EspUsbDeviceConfig identity;identity.product="v3 Vendor soak";
    ok &= device.begin(identity) && device.end()==ESP_OK;
    delay(20);heapBefore=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    received=sent=bad=0;resets=0;
    ok &= device.begin(identity);
    Serial.printf("STARTED %u\r\n",unsigned(ok));
  } else if(s=="finish") {
    const bool ok=device.end()==ESP_OK;delay(20);const size_t after=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    Serial.printf("FINISHED ok=%u rx=%u tx=%u bad=%u resets=%u guards=%u before=%u after=%u\r\n",unsigned(ok),received,sent,bad,resets.load(),unsigned(guards()),unsigned(heapBefore),unsigned(after));
  } else if(s=="stop")Serial.println(device.end()==ESP_OK ? "STOPPED":"STOP_FAILED");
}
