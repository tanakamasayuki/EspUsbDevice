#include <Arduino.h>
#include <EspUsbDevice.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"
EspUsbDevice device;
EspUsbHost host;
EspUsbDeviceVideo *video=nullptr;
alignas(64) uint8_t storage[8192+128];
std::atomic<unsigned> address{0},speed{255},frames{0},results{0},bad{0},expectedIndex{0};
std::atomic<size_t> expectedLength{0};
bool hostStarted=false;
bool stopUsb() {
  if(hostStarted && address.load()) host.videoStop(address.load());
  bool ok=device.end()==ESP_OK;
  if(hostStarted) host.end();
  hostStarted=false;address.store(0);delete video;video=nullptr;
  return ok && heap_caps_check_integrity_all(true);
}
void setup() {
  Serial.begin(115200);Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &i) {
    if(strcmp(i.product,"v3 P4 Video")==0) {speed.store(i.speed);address.store(i.address);}
  });
  host.onVideoFrame([](const EspUsbHostVideoFrame &f) {
    size_t n=expectedLength.load();unsigned index=expectedIndex.load();
    if(!f.complete || !f.data || f.length!=n || f.width!=16 || f.height!=16 || f.format!=ESP_USB_HOST_VIDEO_FORMAT_MJPEG) ++bad;
    for(size_t i=0;i<f.length;++i) if(f.data[i]!=uint8_t(i*37+n+index)) ++bad;
    ++frames;
  });
}
void loop() {
  if(!Serial.available()) {delay(1);return;}
  String command=Serial.readStringUntil('\n');command.trim();
  if(command.startsWith("start ")) {
    char controller[3]={};unsigned bulk=0;sscanf(command.c_str(),"start %2s %u",controller,&bulk);
    bool hs=strcmp(controller,"hs")==0;address.store(0);speed.store(255);frames.store(0);results.store(0);bad.store(0);
    video=new EspUsbDeviceVideo(device,"P4 RAM frames");
    bool ok=video->setFormat(EspUsbDeviceVideoFormat::Mjpeg) && video->setFrameSize(16,16) && video->setMaxFrameSize(8192) && video->setFrameRate(15);
    EspUsbVideoConfig vc;vc.transport=bulk?EspUsbVideoTransport::Bulk:EspUsbVideoTransport::Isochronous;vc.payloadBytes=512;ok &= video->configure(vc)==ESP_OK;
    video->onFrameResult([](const EspUsbTransferResult &r) {
      if(r.buffer!=storage+64 || !r.id || r.requestedBytes!=expectedLength.load() || r.actualBytes!=r.requestedBytes || !r.actualBytesKnown || r.status!=EspUsbTransferStatus::Success || video->frameInFlight()) ++bad;
      ++results;
    });
    EspUsbHostConfig hc;hc.port=hs?ESP_USB_HOST_PORT_FULL_SPEED:ESP_USB_HOST_PORT_HIGH_SPEED;hc.fifo={160,16,16};hostStarted=host.begin(hc);ok &= hostStarted;
    EspUsbDeviceConfig dc;dc.product="v3 P4 Video";dc.controller=hs?EspUsbController::HighSpeed:EspUsbController::FullSpeed;ok &= device.begin(dc);
    uint32_t deadline=millis()+12000;
    while(ok && (!address.load() || !device.ready() || !host.getVideoStreamCount(address.load())) && int32_t(deadline-millis())>0) delay(1);
    ok &= address.load() && device.ready();if(ok) ok &= host.videoStart(ESP_USB_HOST_VIDEO_FORMAT_MJPEG,16,16,15,address.load());
    Serial.printf("START ok=%u controller=%s bulk=%u speed=%u payload=%u error=%u\r\n",unsigned(ok),controller,bulk,speed.load(),unsigned(video->payloadBufferSize()),unsigned(device.lastError()));
  } else if(command=="exercise") {
    bool ok=video && device.ready(),guards=true;unsigned sent=0;size_t bytes=0;
    for(size_t n:{1u,63u,64u,65u,511u,513u,4097u}) {
      if(!ok) break;
      memset(storage,0xc3,sizeof(storage));expectedIndex.store(sent);expectedLength.store(n);
      for(size_t i=0;i<n;++i) storage[64+i]=uint8_t(i*37+n+sent);
      ok &= video->sendFrame(storage+64,n);uint32_t deadline=millis()+5000;
      while(ok && (frames.load()<sent+1 || results.load()<sent+1) && int32_t(deadline-millis())>0) delay(1);
      ok &= frames.load()==sent+1 && results.load()==sent+1 && bad.load()==0;
      for(size_t i=0;i<64;++i) guards &= storage[i]==0xc3;
      for(size_t i=64+n;i<sizeof(storage);++i) guards &= storage[i]==0xc3;
      ok &= guards;if(ok) {++sent;bytes+=n;}
    }
    ok &= heap_caps_check_integrity_all(true);
    Serial.printf("VIDEO ok=%u frames=%u results=%u bytes=%u bad=%u guards=%u\r\n",unsigned(ok),frames.load(),results.load(),unsigned(bytes),bad.load(),unsigned(guards));
  } else if(command=="stop") Serial.println(stopUsb()?"STOPPED":"STOP_FAILED");
}
