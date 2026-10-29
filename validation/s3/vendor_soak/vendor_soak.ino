#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
std::atomic<uint8_t> address{0};
bool started=false;
std::atomic<size_t> receivedCount{0};
std::atomic<bool> receivedBad{false};
uint8_t expected[4097],received[4160];
void disconnectHost(){if(started)host.end();started=false;address=0;}
bool connectHost(){
  address=0;started=host.begin();const uint32_t until=millis()+10000;
  while(started && !address.load() && int32_t(until-millis())>0)delay(1);
  return started && address.load() && host.vendorOpen(address.load(),0xff,ESP_USB_HOST_VENDOR_READ_CONTINUOUS);
}
void setup(){
  Serial.begin(115200);Serial.setTimeout(100);
  host.onVendorData([](const EspUsbHostVendorData &d){
    if(!d.length)return;
    const size_t n=receivedCount.load(std::memory_order_relaxed);
    if(n+d.length>sizeof(received)){receivedBad=true;return;}
    memcpy(received+n,d.data,d.length);
    receivedCount.store(n+d.length,std::memory_order_release);
    uint8_t discard[64];while(host.vendorRead(discard,sizeof(discard),d.address)){}
  });
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &d){if(d.vid==0x303a && d.pid==0x4000 && !strcmp(d.product,"v3 Vendor soak"))address=d.address;});
  host.onDeviceDisconnected([](const EspUsbHostDeviceInfo &){address=0;});
}
void loop(){
  if(!Serial.available()){delay(1);return;}
  String s=Serial.readStringUntil('\n');s.trim();
  if(s.startsWith("run ")) {
    const unsigned frames=s.substring(4).toInt();
    if(!frames || frames>4096){Serial.println("INVALID_RUN");return;}
    bool ok=connectHost();unsigned completed=0,total=0,reconnects=0;
    const unsigned lengths[]={5,63,64,65,129,257,1024,4097};
    for(unsigned sequence=0;sequence<frames && ok;++sequence) {
      const unsigned n=lengths[sequence%8];
      for(unsigned i=0;i<n;++i)expected[i]=uint8_t(i*37+sequence+(i>>8));
      for(unsigned i=0;i<4;++i)expected[i]=uint8_t(sequence>>(8*i));
      receivedCount=0;receivedBad=false;
      ok=host.vendorWrite(expected,n,address.load());
      const uint32_t until=millis()+5000;
      while(ok && receivedCount.load(std::memory_order_acquire)<n && !receivedBad.load() && int32_t(until-millis())>0)delay(1);
      const size_t count=receivedCount.load(std::memory_order_acquire);
      ok &= !receivedBad.load() && count==n && !memcmp(received,expected,n);
      if(!ok){Serial.printf("SOAK_FAILED sequence=%u bytes=%u actual=%u error=%d\r\n",sequence,n,unsigned(count),host.lastError());break;}
      ++completed;total+=n;
      if(completed%25==0 && completed<frames){disconnectHost();ok=connectHost();if(ok)++reconnects;}
    }
    disconnectHost();
    Serial.printf("SOAK ok=%u frames=%u bytes=%u reconnects=%u\r\n",unsigned(ok),completed,total,reconnects);
  } else if(s=="stop"){disconnectHost();Serial.println("STOPPED");}
}
