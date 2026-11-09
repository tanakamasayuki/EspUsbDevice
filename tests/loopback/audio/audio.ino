#include <Arduino.h>
#include <EspUsbDevice.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"

EspUsbDevice device;
EspUsbHost host;
EspUsbAudioFunction *audio=nullptr;
EspUsbAudioPlaybackStream *playback=nullptr;
EspUsbAudioCaptureStream *capture=nullptr;
std::atomic<bool> measuring{false};
std::atomic<unsigned> captured{0},captureBad{0};
std::atomic<unsigned> address{0},speed{255},bad{0},requests{0};
bool hostStarted=false;

bool stopUsb() {
  measuring.store(false);
  if(hostStarted && address.load()) { host.audioInputStop(address.load()); host.audioOutputStop(address.load()); }
  bool ok=device.end()==ESP_OK;
  if(hostStarted) host.end();
  hostStarted=false;address.store(0);delete audio;audio=nullptr;playback=nullptr;capture=nullptr;
  return ok && heap_caps_check_integrity_all(true);
}
void setup() {
  Serial.begin(115200);Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &i) {
    if(strcmp(i.product,"v3 P4 Audio")==0) {speed.store(i.speed);address.store(i.address);}
  });
  host.onAudioData([](const EspUsbHostAudioData &event) {
    if(!measuring.load()) return;
    for(size_t i=0;i<event.length;++i) if(event.data[i]!=0x6b) ++captureBad;
    captured.fetch_add(event.length);
  });
  host.onAudioOutputRequest([](EspUsbHostAudioOutputRequest &request) {
    if(!request.data || request.sampleRate!=16000 || request.channels!=1 || request.bytesPerSample!=2 || request.bitsPerSample!=16 || request.byteCount!=2*request.frameCount) {++bad;return;}
    memset(request.data,0x5a,request.byteCount);request.writtenFrames=request.frameCount;++requests;
  });
}
void loop() {
  if(!Serial.available()) {delay(1);return;}
  String command=Serial.readStringUntil('\n');command.trim();
  if(command.startsWith("start ")) {
    char controller[3]={};unsigned protocol=1;
    sscanf(command.c_str(),"start %2s %u",controller,&protocol);
    const bool hs=strcmp(controller,"hs")==0;
    address.store(0);speed.store(255);bad.store(0);requests.store(0);
    audio=new EspUsbAudioFunction(device,protocol==1?EspUsbAudioProtocol::Uac1:EspUsbAudioProtocol::Uac2);
    playback=&audio->addPlaybackStream();
    capture=&audio->addCaptureStream();
    bool ok=playback->addFormat({16000,1,2,16}) && capture->addFormat({16000,1,2,16});
    EspUsbAudioConfig ac;ac.eventCapacity=0;ac.playbackBufferFrames=ac.captureBufferFrames=2048;ok &= audio->configure(ac)==ESP_OK;
    EspUsbHostConfig hc;hc.port=hs?ESP_USB_HOST_PORT_FULL_SPEED:ESP_USB_HOST_PORT_HIGH_SPEED;
    hostStarted=host.begin(hc);ok &= hostStarted;
    EspUsbDeviceConfig dc;dc.product="v3 P4 Audio";dc.controller=hs?EspUsbController::HighSpeed:EspUsbController::FullSpeed;
    ok &= device.begin(dc);
    const uint32_t deadline=millis()+12000;
    while(ok && (!address.load() || !device.ready() || (!host.audioOutputReady(address.load()) || !host.audioInputReady(address.load()))) && int32_t(deadline-millis())>0) delay(1);
    ok &= address.load() && device.ready() && host.audioOutputReady(address.load()) && host.audioInputReady(address.load());
    Serial.printf("START ok=%u controller=%s protocol=%u speed=%u error=%u\r\n",unsigned(ok),controller,protocol,speed.load(),unsigned(device.lastError()));
  } else if(command=="exercise") {
    bool ok=playback && device.ready();size_t consumed=0;
    playback->clearBuffer();playback->resetStats();bad.store(0);requests.store(0);
    ok &= host.audioOutputStart(1,16,16000,address.load());
    ok &= host.audioInputStart(1,16,16000,address.load());
    captured.store(0);captureBad.store(0);measuring.store(false);
    uint8_t feed[256];memset(feed,0x6b,sizeof(feed));
    const uint32_t start=millis(),deadline=start+450;
    while(ok && int32_t(deadline-millis())>0) {
      capture->write(feed,min(sizeof(feed),capture->availableForWrite()));
      if(!measuring.load() && uint32_t(millis()-start)>=100) {
        capture->resetStats();measuring.store(true);
      }
      uint8_t pcm[256];const size_t n=playback->read(pcm,sizeof(pcm));
      if(measuring.load()) {
        for(size_t i=0;i<n;++i) if(pcm[i]!=0x5a) ++bad;
        consumed+=n;
      }
      delay(1);
    }
    EspUsbAudioFormat current;const bool format=playback->currentFormat(current) && current.sampleRate==16000 && current.channels==1 && current.bitsPerSample==16;
    measuring.store(false);
    const auto captureStats=capture->stats();
    EspUsbAudioFormat captureFormat;
    ok &= capture->currentFormat(captureFormat) && captureFormat.sampleRate==16000 && captureFormat.channels==1 && captureFormat.bitsPerSample==16;
    ok &= captured.load()>=4096 && captureBad.load()==0 && captureStats.underrunCount==0;
    const auto stats=playback->stats();
    const unsigned feedback=host.audioOutputFeedbackUpdates(address.load()),rejects=host.audioOutputFeedbackRejects(address.load());
    const bool hasFeedback=host.audioOutputHasFeedback(address.load());
    const bool expectFeedback=audio->protocol()==EspUsbAudioProtocol::Uac2;
    ok &= consumed>=4096 && bad.load()==0 && requests.load()>0 && format && stats.overrunCount==0 && hasFeedback==expectFeedback && (expectFeedback?feedback>0:feedback==0) && rejects==0;
    ok &= host.audioInputStop(address.load());
    host.audioOutputStop(address.load());
    ok &= heap_caps_check_integrity_all(true);
    Serial.printf("AUDIO ok=%u bytes=%u requests=%u bad=%u format=%u overruns=%u feedback=%u rejects=%u capture=%u capture_bad=%u underruns=%u\r\n",unsigned(ok),unsigned(consumed),requests.load(),bad.load(),unsigned(format),unsigned(stats.overrunCount),feedback,rejects,captured.load(),captureBad.load(),unsigned(captureStats.underrunCount));
  } else if(command=="stop") Serial.println(stopUsb()?"STOPPED":"STOP_FAILED");
}
