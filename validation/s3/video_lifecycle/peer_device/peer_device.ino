#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include <atomic>
EspUsbDevice device;
EspUsbDeviceVideo *video=nullptr;
uint8_t *frame=nullptr;
std::atomic<unsigned> completed{0};
alignas(64) uint8_t externalPayload[1024];
std::atomic<unsigned> results{0}, resultBad{0}, stopped{0}, cancelled{0};
bool metadata() {
  EspUsbDevice d; EspUsbDeviceVideo v(d);
  bool ok=!v.setFormat(static_cast<EspUsbDeviceVideoFormat>(255));
  ok &= !v.setMaxFrameSize(UINT32_MAX) && v.maxFrameSize()==19200;
  ok &= v.setFrameSize(17,17) && !v.setFormat(EspUsbDeviceVideoFormat::Yuy2);
  ok &= v.setFrameSize(16,17) && v.setFormat(EspUsbDeviceVideoFormat::Yuy2) && v.maxFrameSize()==544;
  ok &= !v.setMaxFrameSize(543) && v.maxFrameSize()==544;
  ok &= v.setFrameRate(255) && !v.setFrameSize(4096,4096) && v.width()==16;
  ok &= v.setFrameRate(1) && v.setFrameSize(4096,4096) && !v.setFrameRate(255) && v.frameRate()==1;
  EspUsbDeviceConfig identity; identity.startTinyUsb=false;
  ok &= d.begin(identity) && !v.setFrameSize(16,16) && !v.setFrameRate(15) && !v.setMaxFrameSize(65536);
  ok &= !v.begin() && v.startError()==ESP_ERR_INVALID_STATE;
  ok &= !v.configurationDescriptorForSpeed(nullptr,0,0,1,false);
  ok &= d.end()==ESP_OK && v.setMaxFrameSize(0) && v.setFrameSize(16,16) && v.setFrameRate(15);
  return ok;
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command=Serial.readStringUntil('\n'); command.trim();
  if (command=="metadata") {
    bool ok=metadata(); size_t before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    for (unsigned i=0;i<5 && ok;++i) ok &= metadata();
    Serial.printf("METADATA ok=%u before=%u after=%u\r\n",unsigned(ok),unsigned(before),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  } else if (command.startsWith("start ")) {
    unsigned bulk,payload,external; sscanf(command.c_str(),"start %u %u %u",&bulk,&payload,&external);
    delete video; free(frame); frame=static_cast<uint8_t *>(malloc(65536)); completed=0; results=resultBad=stopped=cancelled=0;
    for (unsigned i=0;frame && i<65536;++i) frame[i]=uint8_t(i*37+13);
    video=new EspUsbDeviceVideo(device,"Video lifecycle");
    bool ok=frame && video->setFrameSize(16,16) && video->setMaxFrameSize(65536);
    EspUsbVideoConfig c; c.transport=bulk ? EspUsbVideoTransport::Bulk : EspUsbVideoTransport::Isochronous;
    c.payloadBytes=payload; if (external) { c.payloadBuffer=externalPayload; c.payloadBufferBytes=sizeof(externalPayload); }
    ok &= video->configure(c)==ESP_OK && video->payloadBufferSize()==0;
    video->onFrameResult([](const EspUsbTransferResult &r) {
      if (!r.id || r.buffer!=frame || !r.requestedBytes || video->frameInFlight()) ++resultBad;
      if (r.status==EspUsbTransferStatus::Success) {
        if (!r.actualBytesKnown || r.actualBytes!=r.requestedBytes) ++resultBad;
      } else if (r.status==EspUsbTransferStatus::Stopped) ++stopped;
      else if (r.status==EspUsbTransferStatus::Cancelled) ++cancelled;
      else ++resultBad;
      ++results;
    });
    video->onFrameComplete([]() { unsigned n=++completed; Serial.printf("COMPLETE %u\r\n",n); });
    ok &= device.begin() && video->payloadBufferSize()==payload && video->onFrameComplete({})==ESP_ERR_INVALID_STATE; Serial.printf("STARTED %u\r\n",unsigned(ok));
  } else if (command=="intruder") {
    auto *v=new EspUsbDeviceVideo(device);
    bool ok=!v->begin() && v->startError()==ESP_ERR_INVALID_STATE; delete v;
    Serial.printf("INTRUDER %u\r\n",unsigned(ok && device.ready()));
  } else if (command.startsWith("frame ")) {
    unsigned n=command.substring(6).toInt();
    Serial.printf("QUEUED %u\r\n",unsigned(video->sendFrame(frame,n)));
  } else if (command=="cancel") {
    uint64_t id=0;
    bool ok=video->submitFrame(frame,65536,id)==ESP_OK && id && video->cancelFrame(id)==ESP_OK;
    const uint32_t until=millis()+5000;
    while (video->frameInFlight() && int32_t(until-millis())>0) delay(1);
    ok &= !video->frameInFlight() && results.load()==5 && cancelled.load()==1 && !resultBad.load();
    ok &= video->cancelFrame(id)==ESP_ERR_NOT_FOUND && video->cancelFrame(0)==ESP_ERR_NOT_FOUND;
    Serial.printf("CANCELLED %u\r\n",unsigned(ok));
  } else if (command=="drop") {
    bool ok=video->sendFrame(frame,65536) && video->frameInFlight();
    delete video; video=nullptr;
    ok &= results.load()==6 && stopped.load()==1 && cancelled.load()==1 && !resultBad.load();
    free(frame); frame=nullptr;
    ok &= !device.ready() && heap_caps_check_integrity_all(true);
    Serial.printf("DROPPED %u\r\n",unsigned(ok));
  } else if (command=="stop") { Serial.println(device.end()==ESP_OK ? "STOPPED" : "STOP_FAILED"); }
}
