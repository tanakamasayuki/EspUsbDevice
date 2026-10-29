#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include <atomic>
#include "../../../../src/internal/EspUsbDwc2Hardware.h"
#include "../../../../src/internal/EspUsbRuntime.h"
std::atomic<unsigned> suspends{0},resumes{0};
extern "C" void espusb_device_event_hook(uint8_t,uint32_t kind,bool) {
  if(kind==ESPUSB_CONTROLLER_EVENT_SUSPEND)++suspends;
  if(kind==ESPUSB_CONTROLLER_EVENT_RESUME)++resumes;
}
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
uint8_t data[64];
void setup(){Serial.begin(115200);Serial.setTimeout(100);}
void loop(){
  if(!Serial.available()){delay(1);return;}String s=Serial.readStringUntil('\n');s.trim();
  if(s=="metadata") {
    bool ok=!device.remoteWakeupEnabled() && device.remoteWakeup()==ESP_ERR_INVALID_STATE;
    for(bool support:{false,true}) {
      EspUsbDeviceConfig c;c.startMode=EspUsbDeviceStartMode::DescriptorsOnly;c.remoteWakeupSupported=support;
      ok &= device.begin(c);const auto *p=device.configurationDescriptor(0);
      ok &= p && bool(p[7]&32)==support && !device.remoteWakeupEnabled() && device.remoteWakeup()==ESP_ERR_INVALID_STATE;
      ok &= device.end()==ESP_OK;
    }
    Serial.printf("METADATA %u\r\n",unsigned(ok));
  } else if(s.startsWith("start ")) {
    EspUsbVendorConfig v;v.rxCapacity=131;v.txCapacity=97;v.transferBytes=64;
    EspUsbDeviceConfig c;c.remoteWakeupSupported=s.substring(6).toInt();
    bool ok=vendor.configure(v)==ESP_OK && device.begin(c);
    Serial.printf("STARTED %u\r\n",unsigned(ok && !device.remoteWakeupEnabled()));
  } else if(s.startsWith("state ")) {
    const bool suspend=s.substring(6).toInt();const uint32_t until=millis()+3000;
    while(device.suspended()!=suspend && int32_t(until-millis())>0)delay(1);
    auto *regs=espusb_dwc2_registers(espusb::internal::usbRuntimeRhport());
    const uint32_t frame=regs->dsts;delay(20);
    Serial.printf("DEVICE_DIAG dsts=%08lx previous=%08lx dctl=%08lx sts=%08lx mask=%08lx suspend=%u resume=%u\r\n", regs->dsts,frame,regs->dctl,regs->gintsts,regs->gintmsk,suspends.load(),resumes.load());
    Serial.printf("STATE ready=%u suspended=%u enabled=%u\r\n",unsigned(device.ready()),unsigned(device.suspended()),unsigned(device.remoteWakeupEnabled()));
  } else if(s=="request") {
    Serial.printf("REQUEST error=%d\r\n",int(device.remoteWakeup()));
  } else if(s=="arm") {
    for(unsigned i=0;i<64;++i)data[i]=uint8_t(i*37+13);
    Serial.printf("ARMED %u\r\n",unsigned(vendor.write(data,64)==64));
  } else if(s=="result") {
    const uint32_t until=millis()+3000;while(vendor.available()<64 && int32_t(until-millis())>0)delay(1);
    bool ok=vendor.read(data,64)==64;for(unsigned i=0;ok && i<64;++i)ok &= data[i]==uint8_t(i*17+7);
    Serial.printf("RESULT %u\r\n",unsigned(ok));
  } else if(s=="stop") {
    bool ok=device.end()==ESP_OK && !device.remoteWakeupEnabled() && device.remoteWakeup()==ESP_ERR_INVALID_STATE && heap_caps_check_integrity_all(true);
    Serial.println(ok ? "STOPPED":"STOP_FAILED");
  }
}
