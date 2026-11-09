#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
#include <hal/usb_dwc_ll.h>
EspUsbHost host;
std::atomic<unsigned> address{0},received{0},bad{0};
bool resumePort() {
  if(!host.ready()) return true;
  if(usb_dwc_ll_hprt_get_port_suspend(USB_DWC_LL_GET_HW(0))) {
    usb_dwc_ll_hprt_set_port_resume(USB_DWC_LL_GET_HW(0));delay(30);
    usb_dwc_ll_hprt_clr_port_resume(USB_DWC_LL_GET_HW(0));delay(20);
  }
  return !usb_dwc_ll_hprt_get_port_suspend(USB_DWC_LL_GET_HW(0));
}
void setup() {
  Serial.begin(115200);Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &i){if(!strcmp(i.product,"v3 CDC TX deadlines")) address=i.address;});
  host.onSerialData([](const EspUsbHostSerialData &data){
    const unsigned at=received.load();
    for(size_t i=0;i<data.length;++i) if(data.data[i]!=uint8_t('A'+(at+i)%26)) ++bad;
    received=at+data.length;
  });
}
void loop() {
  if(!Serial.available()){delay(1);return;}
  String c=Serial.readStringUntil('\n');c.trim();
  if(c=="start") {
    address=0;received=0;bad=0;bool ok=host.begin();const uint32_t until=millis()+10000;
    while(ok && (!address.load() || !host.serialReady(address.load())) && int32_t(until-millis())>0)delay(1);
    Serial.printf("HOST_READY %u\r\n",unsigned(ok && address.load() && host.serialReady(address.load())));
  } else if(c=="arm") {received=0;bad=0;Serial.println("ARMED");
  } else if(c.startsWith("check ")) {
    const unsigned n=c.substring(6).toInt();const uint32_t until=millis()+5000;
    while(received<n && int32_t(until-millis())>0)delay(1);
    Serial.printf("RECEIVED ok=%u bytes=%u bad=%u\r\n",unsigned(received==n && bad==0),received.load(),bad.load());
  } else if(c=="pause") {
    usb_dwc_ll_hprt_set_port_suspend(USB_DWC_LL_GET_HW(0));delay(50);
    Serial.printf("PAUSED %u\r\n",unsigned(usb_dwc_ll_hprt_get_port_suspend(USB_DWC_LL_GET_HW(0))));
  } else if(c=="resume") Serial.printf("RESUMED %u\r\n",unsigned(resumePort()));
  else if(c=="dtr0") {
    size_t actual=0;bool ok=host.vendorControlTransfer(0x21,0x22,0,0,nullptr,0,&actual,address.load(),1000);
    Serial.printf("DTR_CLEARED %u\r\n",unsigned(ok));
  } else if(c=="worker") {
    const uint8_t trigger=1;Serial.printf("TRIGGERED %u\r\n",unsigned(host.sendSerial(&trigger,1,address.load())));
  } else if(c=="stop") {resumePort();host.end();address=0;Serial.println(!host.ready()?"STOPPED":"STOP_FAILED");}
}
