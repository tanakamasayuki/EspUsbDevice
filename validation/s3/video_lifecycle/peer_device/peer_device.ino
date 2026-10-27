#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include <atomic>
EspUsbDevice device;
EspUsbDeviceVideo *video=nullptr;
uint8_t *frame=nullptr;
std::atomic<unsigned> completed{0};
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
  } else if (command=="start") {
    delete video; free(frame); frame=static_cast<uint8_t *>(malloc(65536)); completed=0;
    for (unsigned i=0;frame && i<65536;++i) frame[i]=uint8_t(i*37+13);
    video=new EspUsbDeviceVideo(device,"Video lifecycle");
    bool ok=frame && video->setFrameSize(16,16) && video->setMaxFrameSize(65536);
    video->onFrameComplete([]() { unsigned n=++completed; Serial.printf("COMPLETE %u\r\n",n); });
    ok &= device.begin(); Serial.printf("STARTED %u\r\n",unsigned(ok));
  } else if (command=="intruder") {
    auto *v=new EspUsbDeviceVideo(device);
    bool ok=!v->begin() && v->startError()==ESP_ERR_INVALID_STATE; delete v;
    Serial.printf("INTRUDER %u\r\n",unsigned(ok && device.ready()));
  } else if (command.startsWith("frame ")) {
    unsigned n=command.substring(6).toInt();
    Serial.printf("QUEUED %u\r\n",unsigned(video->sendFrame(frame,n)));
  } else if (command=="drop") {
    bool ok=video->sendFrame(frame,65536) && video->frameInFlight();
    delete video; video=nullptr;
    free(frame); frame=nullptr;
    ok &= !device.ready() && heap_caps_check_integrity_all(true);
    Serial.printf("DROPPED %u\r\n",unsigned(ok));
  } else if (command=="stop") { Serial.println(device.end()==ESP_OK ? "STOPPED" : "STOP_FAILED"); }
}
