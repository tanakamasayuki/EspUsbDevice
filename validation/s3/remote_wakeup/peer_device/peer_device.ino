#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include <atomic>
#include "../../../../src/internal/EspUsbDwc2Hardware.h"
#include "../../../../src/internal/EspUsbRuntime.h"
#include "../../../../src/internal/EspUsbDeviceEndpoint.h"
std::atomic<unsigned> suspends{0},resumes{0},resets{0};
std::atomic<bool> parked{false},released{false};
unsigned resetBefore=0;
extern "C" void espusb_device_event_hook(uint8_t,uint32_t kind,bool) {
  if(kind==ESPUSB_CONTROLLER_EVENT_BUS_RESET)++resets;
  if(kind==ESPUSB_CONTROLLER_EVENT_SUSPEND)++suspends;
  if(kind==ESPUSB_CONTROLLER_EVENT_RESUME)++resumes;
}
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
uint8_t data[64];
std::atomic<bool> raceRun{false},raceEntered{false},raceStopped{false},raceExited{true};
std::atomic<unsigned> accepted{0},denied{0},postStop{0},stoppingCalls{0},raceBad{0};
void requester(void *) {
  while(raceRun.load()) {
    const bool stopped=raceStopped.load(), stopping=espusb::internal::usbRuntimeStopping();
    const esp_err_t error=device.remoteWakeup();const bool enabled=device.remoteWakeupEnabled();
    if(stopped && raceStopped.load()) {++postStop;if(error!=ESP_ERR_INVALID_STATE || enabled)++raceBad;}
    else if(error==ESP_OK)++accepted;
    else if(error==ESP_ERR_INVALID_STATE || error==ESP_ERR_TIMEOUT)++denied;
    else ++raceBad;
    if(stopping && !stopped) {
      ++stoppingCalls;if(error!=ESP_ERR_INVALID_STATE || enabled)++raceBad;
      if(stoppingCalls.load()>=16)released=true;
    }
    raceEntered=true;vTaskDelay(1);
  }
  raceExited=true;vTaskDelete(nullptr);
}
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
  } else if(s=="park") {
    resetBefore=resets.load();released=false;
    bool ok=espusb_device_try_defer([](void *) {parked=true;while(!released.load())vTaskDelay(1);parked=false;},nullptr);
    const uint32_t until=millis()+3000;while(ok && !parked.load() && int32_t(until-millis())>0)delay(1);
    Serial.printf("PARKED %u\r\n",unsigned(ok && parked.load()));
  } else if(s=="wait_reset") {
    const uint32_t until=millis()+3000;while(resets.load()==resetBefore && int32_t(until-millis())>0)delay(1);
    Serial.printf("RESET_BLOCKED %u\r\n",unsigned(resets.load()>resetBefore && parked.load() && !device.ready() && !device.remoteWakeupEnabled()));
  } else if(s=="release") {
    released=true;const uint32_t until=millis()+3000;
    while(parked.load() && int32_t(until-millis())>0)delay(1);
    Serial.printf("RELEASED %u\r\n",unsigned(!parked.load()));
  } else if(s=="request") {
    Serial.printf("REQUEST error=%d\r\n",int(device.remoteWakeup()));
  } else if(s=="arm") {
    for(unsigned i=0;i<64;++i)data[i]=uint8_t(i*37+13);
    Serial.printf("ARMED %u\r\n",unsigned(vendor.write(data,64)==64));
  } else if(s=="result") {
    const uint32_t until=millis()+3000;while(vendor.available()<64 && int32_t(until-millis())>0)delay(1);
    bool ok=vendor.read(data,64)==64;for(unsigned i=0;ok && i<64;++i)ok &= data[i]==uint8_t(i*17+7);
    Serial.printf("RESULT %u\r\n",unsigned(ok));
  } else if(s=="end_race") {
    accepted=denied=postStop=stoppingCalls=raceBad=0;raceEntered=raceStopped=false;raceExited=false;raceRun=true;
    released=false;
    bool parkedOk=espusb_device_try_defer([](void *) {parked=true;while(!released.load())vTaskDelay(1);parked=false;},nullptr);
    uint32_t parkUntil=millis()+3000;while(parkedOk && !parked.load() && int32_t(parkUntil-millis())>0)delay(1);
    parkedOk &= parked.load();
    bool ok=xTaskCreate(requester,"wake-requester",4096,nullptr,2,nullptr)==pdPASS;
    if(!ok){raceRun=false;raceExited=true;}
    uint32_t until=millis()+3000;while(ok && !raceEntered.load() && int32_t(until-millis())>0)delay(1);
    ok &= parkedOk && raceEntered.load() && accepted.load()>0;
    ok &= device.end()==ESP_OK;raceStopped=true;
    until=millis()+3000;while(ok && postStop.load()<32 && int32_t(until-millis())>0)delay(1);
    raceRun=false;until=millis()+3000;while(!raceExited.load() && int32_t(until-millis())>0)delay(1);
    delay(20);
    ok &= raceExited.load() && stoppingCalls.load()>=16 && postStop.load()>=32 && !raceBad.load() && heap_caps_check_integrity_all(true);
    Serial.printf("END_RACE ok=%u accepted=%u denied=%u stopping=%u post=%u bad=%u\r\n",unsigned(ok),accepted.load(),denied.load(),stoppingCalls.load(),postStop.load(),raceBad.load());
  } else if(s=="stop") {
    released=true;
    bool ok=device.end()==ESP_OK && !device.remoteWakeupEnabled() && device.remoteWakeup()==ESP_ERR_INVALID_STATE && heap_caps_check_integrity_all(true);
    Serial.println(ok ? "STOPPED":"STOP_FAILED");
  }
}
