#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
#include "../../../../src/internal/EspUsbDeviceEndpoint.h"
#include "../../../../src/internal/EspUsbTinyUsbRuntime.h"
EspUsbDevice device;
EspUsbDeviceNet first(device), second(device);
EspUsbDeviceNet *net[] = {&first,&second};
std::atomic<unsigned> rejected{0};
bool armed(unsigned i,unsigned which) {
  const auto *d=device.configurationDescriptor(0);if(!d || i>=2 || which>2)return false;
  const unsigned bytes=d[2]|unsigned(d[3])<<8;unsigned interface=255;uint8_t endpoint=0;
  for(unsigned n=9;n+2<=bytes && d[n]>=2 && d[n]<=bytes-n;n+=d[n]) {
    if(d[n+1]==4 && d[n]==9)interface=d[n+2];
    if(d[n+1]==5 && d[n]==7 && ((which==2 && interface==2*i && (d[n+3]&3)==3) ||
        (which<2 && interface==2*i+1 && (d[n+3]&3)==2 && bool(d[n+2]&128)==bool(which))))endpoint=d[n+2];
  }
  const uint8_t port=espusb::internal::tinyUsbRuntimeRhport();const uint32_t until=millis()+2000;
  while(endpoint && !espusb_device_edpt_busy(port,endpoint) && int32_t(until-millis())>0)delay(1);
  return endpoint && espusb_device_edpt_busy(port,endpoint) && !espusb_device_edpt_stalled(port,endpoint);
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  for (unsigned i = 0; i < 2; ++i) net[i]->onFrame([i](const uint8_t *p,size_t n) { if (!net[i]->sendFrame(p,n)) ++rejected; });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    if(device.ready()){Serial.println("STARTED 0");return;}rejected=0;
    EspUsbNetConfig c; c.ntbOutBytes = 16384; c.txBuffers = c.rxBuffers = 2;
    bool ok = first.configure(c) == ESP_OK && second.configure(c) == ESP_OK;
    ok &= device.begin() && memcmp(first.macAddress(),second.macAddress(),6) != 0;
    Serial.printf("STARTED %u\r\n",unsigned(ok));
  } else if(command.startsWith("armed ")) {
    unsigned i,which;sscanf(command.c_str(),"armed %u %u",&i,&which);Serial.printf("ARMED %u\r\n",unsigned(armed(i,which)));
  } else if(command.startsWith("send ") || command.startsWith("blocked ")) {
    const bool blocked=command.startsWith("blocked ");unsigned i,tag=0x72;
    sscanf(command.c_str(),blocked ? "blocked %u":"send %u %u",&i,&tag);uint8_t data[64];
    for(unsigned j=0;j<64;++j)data[j]=uint8_t((tag+j)%251);
    const bool sent=net[i]->sendFrame(data,sizeof(data));
    Serial.printf("SENT %u\r\n",unsigned((blocked ? !sent:sent) && net[i]->linkUp()));
  } else if(command.startsWith("ready ")) {
    const unsigned i=command.substring(6).toInt();const uint32_t until=millis()+2000;
    while(i<2 && !net[i]->linkUp() && int32_t(until-millis())>0)delay(1);
    Serial.printf("READY %u\r\n",unsigned(i<2 && net[i]->linkUp()));
  } else if (command == "stats") {
    const uint32_t until = millis()+2000;
    while ((first.stats().txFrames < 43 || second.stats().txFrames < 1) && int32_t(until-millis())>0) delay(1);
    for (unsigned i = 0; i < 2; ++i) { auto s = net[i]->stats(); Serial.printf("STATS %u tx=%u rx=%u invalid=%u\r\n",i,unsigned(s.txFrames),unsigned(s.rxFrames),unsigned(s.invalidNtbs)); }
    Serial.printf("REJECTED %u\r\n",unsigned(rejected.load()));
  } else if (command=="ip") {
    bool ok=first.ipConfig(IPAddress(192,168,7,1),IPAddress(192,168,7,1),IPAddress(255,255,255,0))==ESP_OK;
    ok &= second.ipConfig(IPAddress(192,168,8,1),IPAddress(192,168,8,1),IPAddress(255,255,255,0))==ESP_OK;
    ok &= first.beginNetwork() && second.beginNetwork() && first.localIP()==IPAddress(192,168,7,1) && second.localIP()==IPAddress(192,168,8,1);
    ok &= first.endNetwork() && second.networkUp() && first.beginNetwork() && first.networkUp() && second.networkUp();
    ok &= first.endNetwork() && second.endNetwork();
    Serial.printf("IP_PAIR %u\r\n",unsigned(ok));
  } else if (command == "stop") { Serial.println(device.end() == ESP_OK ? "STOPPED" : "STOP_FAILED"); }
}
