#include <Arduino.h>
#include <EspUsbDevice.h>
#include <algorithm>
#include "../../../../src/internal/EspUsbDwc2Hardware.h"
#include "../../../../src/internal/EspUsbDwc2Platform.h"
#include "../../../../src/internal/EspUsbRuntime.h"
#include "../../common/probe_link.h"
// Highest endpoint number of the controller Auto selects (S3 FS: 6, P4 HS: 15).
constexpr unsigned MAX_EP=PROBE_BULK_MPS==512 ? 15 : 6;
unsigned irqCalls=0;
extern "C" esp_err_t espusb_dwc2_allocate_interrupt(int source,int flags,intr_handler_t handler,void *context,intr_handle_t *result) {
  ++irqCalls;return esp_intr_alloc(source,flags,handler,context,result);
}
EspUsbDevice device;
class SparseVendor : public EspUsbDeviceVendor {
public:
  SparseVendor(EspUsbDevice &d,uint8_t endpoint):EspUsbDeviceVendor(d),endpoint_(endpoint){}
  uint8_t endpointCount() const override{return endpoint_;}
  uint16_t configurationDescriptor(uint8_t *dst,uint8_t itf,uint8_t,uint16_t packet) override {
    return EspUsbDeviceVendor::configurationDescriptor(dst,itf,endpoint_,packet);
  }
  uint8_t endpoint_;
};
SparseVendor *vendor=nullptr;
// Odd, unaligned rings (97/131 on S3) that still absorb a whole packet; one guard byte each side.
uint8_t rx[PROBE_BULK_MPS+35],tx[2*PROBE_BULK_MPS+5];
void setup(){Serial.begin(115200);Serial.setTimeout(100);}
void loop(){
  if(vendor){uint8_t b[64];size_t n=vendor->read(b,std::min(sizeof(b),vendor->writeAvailable()));if(n)vendor->write(b,n);}
  if(!Serial.available()){delay(1);return;}
  String s=Serial.readStringUntil('\n');s.trim();
  if(s.startsWith("start ")) {
    const unsigned ep=s.substring(6).toInt();
    if(device.started() || !ep || ep>MAX_EP+1){Serial.println("INVALID_START");return;}
    delete vendor;vendor=new SparseVendor(device,ep);
    memset(rx,0xc3,sizeof(rx));memset(tx,0xc3,sizeof(tx));
    EspUsbBulkStreamConfig c;c.rxCapacity=sizeof(rx)-2;c.txCapacity=sizeof(tx)-2;c.transferBytes=PROBE_BULK_MPS;c.rxBuffer=rx+1;c.txBuffer=tx+1;
    bool ok=vendor->configure(c)==ESP_OK;
    EspUsbDeviceConfig identity;identity.product="v3 Vendor API probe";
    const unsigned before=irqCalls;ok &= device.begin(identity);
    Serial.printf("STARTED ep=%u ok=%u error=%d irq=%u\r\n",ep,unsigned(ok),int(device.lastError()),irqCalls-before);
  } else if(s=="stats") {
    const unsigned ep=vendor->endpoint_;
    const bool guards=rx[0]==0xc3 && rx[sizeof(rx)-1]==0xc3 && tx[0]==0xc3 && tx[sizeof(tx)-1]==0xc3;
    Serial.printf("ENDPOINT ep=%u fifo=%u guards=%u\r\n",ep,unsigned((espusb_dwc2_registers(espusb::internal::usbRuntimeRhport())->epin[ep].ctl>>22)&15),unsigned(guards));
  } else if(s=="stop")Serial.println(device.end()==ESP_OK ? "STOPPED":"STOP_FAILED");
}
