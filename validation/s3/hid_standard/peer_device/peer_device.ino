#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include <atomic>
#include "../../common/probe_link.h"
// Two full interrupt packets per report: HID MPS is 64 on FS, 1024 on HS.
constexpr unsigned BYTES=2*(PROBE_BULK_MPS==64 ? 64:1024);
EspUsbDevice device;
EspUsbDeviceHidVendor hid(device,BYTES);
uint8_t payload[BYTES];
std::atomic<unsigned> outputs{0},bad{0};
void setup() {
  Serial.begin(115200);Serial.setTimeout(100);
  for(unsigned i=0;i<BYTES;++i)payload[i]=uint8_t(i*37+13);
  hid.onOutputReport([](const EspUsbDeviceHidReport &r){
    if(r.length!=BYTES || memcmp(r.data,payload,BYTES))++bad;
    ++outputs;
  });
}
void loop() {
  if(!Serial.available()){delay(1);return;}
  String s=Serial.readStringUntil('\n');s.trim();
  if(s=="start") {outputs=bad=0;Serial.printf("STARTED %u\r\n",unsigned(device.begin()));}
  else if(s=="send")Serial.printf("SENT %u\r\n",unsigned(hid.sendInput(payload,sizeof(payload),0)));
  else if(s.startsWith("stats ")) {
    unsigned expected=s.substring(6).toInt();uint32_t until=millis()+3000;
    while(outputs.load()<expected && int32_t(until-millis())>0)delay(1);
    Serial.printf("STATS outputs=%u bad=%u\r\n",outputs.load(),bad.load());
  } else if(s=="stop")Serial.println(device.end()==ESP_OK && heap_caps_check_integrity_all(true) ? "STOPPED":"STOP_FAILED");
}
