#include <Arduino.h>
#include <EspUsbDevice.h>
#include <algorithm>
EspUsbDevice device;
EspUsbAudioFunction *audio[4]={};
EspUsbAudioPlaybackStream *playback[4]={};
EspUsbAudioCaptureStream *capture[4]={};
unsigned consumed[4]={},bad[4]={};
void setup(){Serial.begin(115200);Serial.setTimeout(100);}
void loop(){
  for(unsigned i=0;i<4;++i)if(audio[i]) {
    uint8_t data[256];size_t n=playback[i]->read(data,sizeof(data));consumed[i]+=n;
    for(size_t b=0;b<n;++b)if(data[b]!=0x50+i)++bad[i];
    n=std::min(sizeof(data),capture[i]->availableForWrite());
    if(n){memset(data,0x50+i,n);capture[i]->write(data,n);}
  }
  if(!Serial.available()){delay(1);return;}
  String s=Serial.readStringUntil('\n');s.trim();
  if(s=="start") {
    if(device.started()){Serial.println("INVALID_START");return;}
    bool ok=true;
    for(unsigned i=0;i<4;++i) {
      consumed[i]=bad[i]=0;
      if(audio[i])continue;
      audio[i]=new EspUsbAudioFunction(device,EspUsbAudioProtocol::Uac1);
      playback[i]=&audio[i]->addPlaybackStream();capture[i]=&audio[i]->addCaptureStream();
      ok &= playback[i]->addFormat({24000,2,2,16}) && capture[i]->addFormat({24000,2,2,16});
      EspUsbAudioConfig c;c.eventCapacity=0;c.playbackBufferFrames=c.captureBufferFrames=65;
      ok &= audio[i]->configure(c)==ESP_OK;
    }
    EspUsbDeviceConfig c;c.product="v3 four Audio functions";ok &= device.begin(c);
    Serial.printf("STARTED ok=%u error=%d span=%u\r\n",unsigned(ok),int(device.lastError()),unsigned(audio[0]->endpointCount()));
  } else if(s.startsWith("stats ")) {
    unsigned i=s.substring(6).toInt();if(i>=4)return;
    const auto a=playback[i]->stats(),b=capture[i]->stats();
    Serial.printf("FUNCTION index=%u rx=%u tx=%u consumed=%u bad=%u\r\n",i,unsigned(a.transferredBytes),unsigned(b.transferredBytes),consumed[i],bad[i]);
  } else if(s=="stop")Serial.println(device.end()==ESP_OK ? "STOPPED":"STOP_FAILED");
}
