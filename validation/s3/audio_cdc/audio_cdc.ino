#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
std::atomic<uint8_t> address{0};
std::atomic<unsigned> pcmBytes{0},pcmBad{0},filled{0};
std::atomic<size_t> serialCount{0};
std::atomic<bool> serialBad{false};
uint8_t serialInput[256];
void setup() {
  Serial.begin(115200);Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &d){if(d.vid==0x303a && d.pid==0x4000)address=d.address;});
  host.onAudioData([](const EspUsbHostAudioData &d){
    for(size_t i=0;i<d.length;++i){if(d.data[i]!=0 && d.data[i]!=0x5a)++pcmBad;if(d.data[i]==0x5a)++filled;}
    pcmBytes.fetch_add(d.length);
  });
  host.onAudioOutputRequest([](EspUsbHostAudioOutputRequest &r){memset(r.data,0x5a,r.byteCount);r.writtenFrames=r.frameCount;});
  host.onSerialData([](const EspUsbHostSerialData &d){
    if(d.port || !d.length)return;
    const size_t n=serialCount.load(std::memory_order_relaxed);
    if(n+d.length>sizeof(serialInput)){serialBad=true;return;}
    memcpy(serialInput+n,d.data,d.length);serialCount.store(n+d.length,std::memory_order_release);
  });
}
void loop() {
  if(!Serial.available()){delay(1);return;}
  String s=Serial.readStringUntil('\n');s.trim();
  if(s=="start") {
    address=0;EspUsbHostConfig config;config.fifo={64,32,64};
    bool ok=host.begin(config);const uint32_t until=millis()+10000;
    while(ok && (!address.load() || !host.serialReady(address.load(),0)) && int32_t(until-millis())>0)delay(1);
    ok &= address.load() && host.serialReady(address.load(),0) && host.serialPortCount(address.load())==1;
    Serial.printf("HOST_READY %u\r\n",unsigned(ok));
  } else if(s.startsWith("run ")) {
    const unsigned rate=s.substring(4).toInt();pcmBytes=pcmBad=filled=0;serialBad=false;
    bool ok=host.audioOutputStart(2,16,rate,address.load()) && host.audioInputStart(2,16,rate,address.load());
    unsigned echoes=0,bytes=0,overlap=0;bool line=false;
    const unsigned lengths[]={1,63,64,65,129};const uint32_t until=millis()+1500;
    while(ok && int32_t(until-millis())>0) {
      const unsigned n=lengths[echoes%5];uint8_t data[129];
      for(unsigned i=0;i<n;++i)data[i]=uint8_t(i*37+echoes);
      serialCount.store(0,std::memory_order_release);const unsigned before=pcmBytes.load();
      ok=host.sendSerial(data,n,address.load(),0);const uint32_t deadline=millis()+3000;
      while(ok && serialCount.load(std::memory_order_acquire)<n && !serialBad.load() && int32_t(deadline-millis())>0)delay(1);
      if(!ok || serialCount.load(std::memory_order_acquire)!=n)Serial.printf("CDC_DIAG sent=%u expected=%u actual=%u bad=%u error=%d\r\n",unsigned(ok),n,unsigned(serialCount.load()),unsigned(serialBad.load()),int(host.lastError()));
      ok &= !serialBad.load() && serialCount.load(std::memory_order_acquire)==n && !memcmp(serialInput,data,n);
      if(ok){++echoes;bytes+=n;if(pcmBytes.load()>before)++overlap;}
      if(echoes==10 && !line){EspUsbHostSerialConfig c;c.baud=230400;line=host.setSerialConfig(c,address.load(),0);ok &= line;}
    }
    const unsigned feedback=host.audioOutputFeedbackUpdates(address.load()),rejects=host.audioOutputFeedbackRejects(address.load());
    ok &= host.audioInputStop(address.load());host.audioOutputStop(address.load());
    ok &= line && echoes>=10 && overlap && pcmBytes.load()>=rate*2 && filled.load() && !pcmBad.load();
    Serial.printf("CONCURRENT ok=%u pcm=%u filled=%u bad=%u echoes=%u bytes=%u overlap=%u feedback=%u rejects=%u\r\n",unsigned(ok),pcmBytes.load(),filled.load(),pcmBad.load(),echoes,bytes,overlap,feedback,rejects);
  } else if(s=="stop"){host.end();Serial.println(!host.ready() ? "STOPPED":"STOP_FAILED");}
}
