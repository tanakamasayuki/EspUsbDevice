#include <Arduino.h>
#include <EspUsbDevice.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"
EspUsbDevice device;
EspUsbDeviceNet net(device);
EspUsbHost host;
std::atomic<unsigned> address{0},speed{255},frames{0},bad{0},echoed{0};
std::atomic<size_t> expected{0};
bool hostStarted=false;
uint8_t frame[1514+64];
uint8_t pattern(size_t i,size_t n) { return uint8_t(i*37+n); }
bool valid(const uint8_t *data,size_t n) {
  if(!data || n!=expected.load()) return false;
  for(size_t i=0;i<n;++i) if(data[i]!=pattern(i,n)) return false;
  return true;
}
bool stopUsb() {
  bool ok=device.end()==ESP_OK;
  if(hostStarted) host.end();
  Serial.printf("HOST_STOP error=%u\r\n",unsigned(host.lastError()));
  hostStarted=false;address.store(0);
  return ok && net.ntbInCapacity()==0 && net.ntbOutCapacity()==0 && heap_caps_check_integrity_all(true);
}
void setup() {
  Serial.begin(115200);Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if(strcmp(info.product,"v3 P4 NCM")==0) {speed.store(info.speed);address.store(info.address);}
  });
  net.onFrame([](const uint8_t *data,size_t n) {
    if(!valid(data,n) || !net.sendFrame(data,n)) ++bad;
    else ++echoed;
  });
  host.onNetworkFrame([](const EspUsbHostNetworkFrame &f) {
    if(!valid(f.data,f.length)) ++bad;
    ++frames;
  });
}
void loop() {
  if(!Serial.available()) {delay(1);return;}
  String command=Serial.readStringUntil('\n');command.trim();
  if(command.startsWith("start ")) {
    char controller[3]={};unsigned capacity=2048;
    sscanf(command.c_str(),"start %2s %u",controller,&capacity);
    bool hs=strcmp(controller,"hs")==0;
    address=0;speed=255;frames=0;bad=0;echoed=0;
    EspUsbNetConfig c;c.ntbInBytes=capacity;c.ntbOutBytes=capacity;
    c.maxDatagrams=capacity==2048?1:8;c.txBuffers=2;c.rxBuffers=2;
    bool ok=net.configure(c)==ESP_OK;
    EspUsbHostConfig hc;hc.port=hs?ESP_USB_HOST_PORT_FULL_SPEED:ESP_USB_HOST_PORT_HIGH_SPEED;
    hostStarted=host.begin(hc);ok &= hostStarted;
    Serial.printf("HOST_START ok=%u error=%u\r\n",unsigned(hostStarted),unsigned(host.lastError()));
    EspUsbDeviceConfig dc;dc.product="v3 P4 NCM";dc.controller=hs?EspUsbController::HighSpeed:EspUsbController::FullSpeed;
    ok &= device.begin(dc);
    const uint32_t until=millis()+12000;
    while(ok && (!address.load() || !device.ready()) && int32_t(until-millis())>0) delay(1);
    ok &= address.load() && device.ready();
    if(ok) ok &= host.networkOpen(address.load());
    const uint32_t linkUntil=millis()+5000;
    while(ok && (!host.networkLinkUp(address.load()) || !net.linkUp()) && int32_t(linkUntil-millis())>0) delay(1);
    ok &= host.networkReady(address.load()) && host.networkLinkUp(address.load()) && net.linkUp();
    Serial.printf("START ok=%u controller=%s capacity=%u speed=%u in=%u out=%u error=%u\r\n",unsigned(ok),controller,capacity,speed.load(),unsigned(net.ntbInCapacity()),unsigned(net.ntbOutCapacity()),unsigned(device.lastError()));
  } else if(command=="exercise") {
    bool ok=true;size_t bytes=0;unsigned sent=0;
    for(size_t n:{64u,65u,127u,128u,129u,511u,512u,513u,1514u}) {
      memset(frame,0xc3,sizeof(frame));
      for(size_t i=0;i<n;++i) frame[i]=pattern(i,n);
      expected=n;
      ok &= host.networkWriteFrame(frame,n,address.load());
      const uint32_t until=millis()+3000;
      while(frames.load()<sent+1 && int32_t(until-millis())>0) delay(1);
      ok &= frames.load()==sent+1 && echoed.load()==sent+1 && bad.load()==0;
      for(size_t i=n;i<sizeof(frame);++i) ok &= frame[i]==0xc3;
      if(!ok) break;
      ++sent;bytes+=n;
    }
    const uint32_t until=millis()+2000;
    while(net.stats().txFrames<sent && int32_t(until-millis())>0) delay(1);
    const auto stats=net.stats();
    ok &= stats.rxFrames==9 && stats.txFrames==9 && stats.invalidNtbs==0 && heap_caps_check_integrity_all(true);
    Serial.printf("NCM ok=%u frames=%u echoes=%u bytes=%u rx=%u tx=%u invalid=%u bad=%u\r\n",unsigned(ok),frames.load(),echoed.load(),unsigned(bytes),unsigned(stats.rxFrames),unsigned(stats.txFrames),unsigned(stats.invalidNtbs),bad.load());
  } else if(command=="stop") Serial.println(stopUsb()?"STOPPED":"STOP_FAILED");
}
