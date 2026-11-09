#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
std::atomic<uint8_t> address{0};
std::atomic<unsigned> frames{0}, bad{0}, length{0};
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &d) { if (d.vid==0x303a && d.pid==0x4000) address=d.address; });
  host.onVideoFrame([](const EspUsbHostVideoFrame &f) {
    if (!f.complete) ++bad;
    for (size_t i=0;i<f.length;++i) if (f.data[i]!=uint8_t(i*37+13)) ++bad;
    length=f.length; ++frames;
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command=Serial.readStringUntil('\n'); command.trim();
  if (command=="start") {
    address=0; frames=bad=length=0;
    EspUsbHostConfig c; c.fifo={160,16,16}; bool ok=host.begin(c);
    const uint32_t until=millis()+10000;
    while (ok && !address.load() && int32_t(until-millis())>0) delay(1);
    ok &= address.load() && host.videoStart(ESP_USB_HOST_VIDEO_FORMAT_MJPEG,16,16,15,address.load());
    Serial.printf("HOST_READY %u\r\n",unsigned(ok));
  } else if (command.startsWith("check ")) {
    unsigned count,n; sscanf(command.c_str(),"check %u %u",&count,&n);
    const uint32_t until=millis()+5000;
    while (frames.load()<count && int32_t(until-millis())>0) delay(1);
    Serial.printf("FRAME %u count=%u length=%u bad=%u\r\n",unsigned(frames.load()==count && length.load()==n && !bad.load()),unsigned(frames.load()),unsigned(length.load()),unsigned(bad.load()));
  } else if (command=="stop") { host.end(); Serial.println(!host.ready() ? "STOPPED" : "STOP_FAILED"); }
}
