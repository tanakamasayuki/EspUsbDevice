#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
std::atomic<uint8_t> address{0};
std::atomic<size_t> counts[2];
uint8_t input[2][256];
bool started=false;
void setup(){
  Serial.begin(115200);Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &d){if(d.vid==0x303a && d.pid==0x4000)address=d.address;});
  host.onSerialData([](const EspUsbHostSerialData &d){
    if(d.port>=2)return;const size_t n=counts[d.port].load();
    if(n+d.length<=sizeof(input[0]))memcpy(input[d.port]+n,d.data,d.length);
    counts[d.port].store(n+d.length,std::memory_order_release);
  });
}
void loop(){
  if(!Serial.available()){delay(1);return;}
  String s=Serial.readStringUntil('\n');s.trim();
  if(s=="start") {
    address=0;started=host.begin();const uint32_t until=millis()+10000;
    while(started && (!address.load() || !host.serialReady(address.load(),0) || !host.serialReady(address.load(),1)) && int32_t(until-millis())>0)delay(1);
    Serial.printf("HOST_READY %u\r\n",unsigned(address.load() && host.serialPortCount(address.load())==2 && host.serialReady(address.load(),0) && host.serialReady(address.load(),1)));
  } else if(s.startsWith("exchange ")) {
    unsigned port,n;sscanf(s.c_str(),"exchange %u %u",&port,&n);
    if(port>1 || n>129 || !n){Serial.println("INVALID_EXCHANGE");return;}
    uint8_t b[129];for(unsigned i=0;i<n;++i)b[i]=uint8_t(i*37+n);
    counts[port]=0;bool ok=host.sendSerial(b,n,address.load(),port);const uint32_t until=millis()+3000;
    while(ok && counts[port].load(std::memory_order_acquire)<n && int32_t(until-millis())>0)delay(1);
    ok &= counts[port].load(std::memory_order_acquire)==n && !memcmp(input[port],b,n);
    Serial.printf("EXCHANGE port=%u bytes=%u ok=%u\r\n",port,n,unsigned(ok));
  } else if(s=="stop") {if(started)host.end();started=false;address=0;Serial.println("STOPPED");}
}
