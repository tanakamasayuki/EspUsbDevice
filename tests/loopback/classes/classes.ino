#include <Arduino.h>
#include <EspUsbDevice.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"

EspUsbDevice device;
EspUsbDeviceMsc disk(device);
EspUsbDeviceMidi midi(device,2);
EspUsbHost host;
uint8_t storage[64*512+128], sent[7*512], received[7*512];
EspUsbDeviceMscRamDisk *ram=nullptr;
std::atomic<unsigned> address{0},speed{255},messages{0},bad{0};
bool hostStarted=false;
unsigned sectorSize=512;

bool stopUsb() {
  bool ok=device.end()==ESP_OK;
  if(hostStarted) host.end();
  hostStarted=false; address.store(0);
  delete ram; ram=nullptr;
  return ok && heap_caps_check_integrity_all(true);
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if(strcmp(info.product,"v3 P4 MSC MIDI")==0) {speed.store(info.speed);address.store(info.address);}
  });
  host.onMidiMessage([](const EspUsbHostMidiMessage &event) {
    unsigned i=messages.load();
    const uint8_t expected[]={uint8_t((i%2)<<4|9),uint8_t(0x90|i%16),uint8_t(i%128),uint8_t((i*3+7)%128)};
    if(!event.raw || event.length!=4 || memcmp(expected,event.raw,4)) ++bad;
    ++messages;
  });
  midi.onPacket([](const EspUsbDeviceMidiPacket &packet) {if(!midi.writePacket(packet)) ++bad;});
}
void loop() {
  if(!Serial.available()) {delay(1);return;}
  String command=Serial.readStringUntil('\n');command.trim();
  if(command.startsWith("start ")) {
    char controller[3]={};unsigned staging=64;
    sscanf(command.c_str(),"start %2s %u %u",controller,&sectorSize,&staging);
    const bool hs=strcmp(controller,"hs")==0;
    memset(storage,0xc3,sizeof(storage)); address.store(0);speed.store(255);messages.store(0);bad.store(0);
    bool ok=sectorSize==511 || sectorSize==512;
    ram=new EspUsbDeviceMscRamDisk(storage+64,64,sectorSize);
    ok &= ram && ram->valid() && ram->attach(disk);
    if(ram) ram->clear(0x5a);
    disk.mediaPresent(true);disk.isWritable(true);
    EspUsbMscConfig mc;mc.transferBytes=staging;ok &= disk.configure(mc)==ESP_OK;
    EspUsbMidiConfig mi;mi.inCables=mi.outCables=2;mi.rxMode=EspUsbMidiRxMode::Callback;mi.rxPackets=0;mi.txPackets=32;mi.transferPackets=16;
    ok &= midi.configure(mi)==ESP_OK;
    EspUsbHostConfig hc;hc.port=hs?ESP_USB_HOST_PORT_FULL_SPEED:ESP_USB_HOST_PORT_HIGH_SPEED;
    hostStarted=host.begin(hc);ok &= hostStarted;
    EspUsbDeviceConfig dc;dc.product="v3 P4 MSC MIDI";dc.controller=hs?EspUsbController::HighSpeed:EspUsbController::FullSpeed;
    bool started=device.begin(dc);
    if(hs && staging==64) {
      const bool rejected=!started && device.lastError()==ESP_ERR_INVALID_SIZE && disk.transferCapacity()==0;
      ok &= rejected && device.end()==ESP_OK;
      mc.transferBytes=512; ok &= disk.configure(mc)==ESP_OK;
      Serial.printf("CAPACITY_REJECT ok=%u requested=64 retry=512\r\n",unsigned(rejected));
      started=device.begin(dc);
    }
    ok &= started;
    const uint32_t deadline=millis()+12000;
    while(ok && (!address.load() || !host.mscReady(address.load()) || !host.midiReady(address.load()) || !device.ready()) && int32_t(deadline-millis())>0) delay(1);
    uint32_t count=0,size=0;
    ok &= address.load() && device.ready() && host.midiReady(address.load()) && host.mscCapacity(count,size,address.load()) && count==64 && size==sectorSize;
    Serial.printf("START ok=%u controller=%s speed=%u block=%u staging=%u\r\n",unsigned(ok),controller,speed.load(),sectorSize,unsigned(disk.transferCapacity()));
  } else if(command=="exercise") {
    bool ok=ram && device.ready();unsigned writes=0,reads=0;
    const unsigned lbas[]={0,3,57};
    for(unsigned lba:lbas) {
      for(size_t i=0;i<7*sectorSize;++i) sent[i]=uint8_t(i*13+lba+sectorSize);
      ok &= host.mscWriteBlocks(lba,sent,7,address.load());
      if(ok) ++writes;
      memset(received,0,sizeof(received));
      ok &= host.mscReadBlocks(lba,received,7,address.load()) && !memcmp(received,sent,7*sectorSize);
      ok &= !memcmp(ram->data()+lba*sectorSize,sent,7*sectorSize);
      if(ok) ++reads;
    }
    unsigned guard=1;
    for(size_t i=0;i<64;++i) guard &= storage[i]==0xc3;
    for(size_t i=64+64*sectorSize;i<sizeof(storage);++i) guard &= storage[i]==0xc3;
    ok &= guard && host.mscSynchronizeCache(address.load());
    messages.store(0);bad.store(0);
    for(unsigned i=0;i<32 && ok;++i) {
      const uint8_t packet[]={uint8_t((i%2)<<4|9),uint8_t(0x90|i%16),uint8_t(i%128),uint8_t((i*3+7)%128)};
      ok &= host.midiSend(packet,sizeof(packet),address.load());
      const uint32_t deadline=millis()+2000;
      while(ok && messages.load()<i+1 && int32_t(deadline-millis())>0) delay(1);
      ok &= messages.load()==i+1 && bad.load()==0;
    }
    ok &= heap_caps_check_integrity_all(true);
    Serial.printf("CLASSES ok=%u writes=%u reads=%u midi=%u bad=%u guards=%u\r\n",unsigned(ok),writes,reads,messages.load(),bad.load(),guard);
  } else if(command=="stop") Serial.println(stopUsb()?"STOPPED":"STOP_FAILED");
}
