#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include <atomic>
EspUsbDevice device;
EspUsbDeviceHidVendor hid(device,128);
uint8_t payload[128];
std::atomic<unsigned> outputs{0},bad{0};
void setup() {
  Serial.begin(115200);Serial.setTimeout(100);
  for(unsigned i=0;i<128;++i)payload[i]=uint8_t(i*37+13);
  hid.onOutputReport([](const EspUsbDeviceHidReport &r){
    if(r.length!=128 || memcmp(r.data,payload,128))++bad;
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
