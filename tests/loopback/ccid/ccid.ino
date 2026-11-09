#include <Arduino.h>
#include <EspUsbDevice.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"
EspUsbDevice device;
EspUsbDeviceCcid card(device);
EspUsbHost host;
std::atomic<unsigned> address{0},speed{255},apdus{0},escapes{0};
bool hostStarted=false,removable=true;
const uint8_t expectedAtr[]={0x3b,0x80,0x01,0x81};
uint8_t sent[600],received[1024];

bool stopUsb() {
  if(hostStarted && address.load()) host.ccidClose(address.load());
  bool ok=device.end()==ESP_OK;
  if(hostStarted) host.end();
  hostStarted=false;address.store(0);
  return ok && card.messageCapacity()==0 && heap_caps_check_integrity_all(true);
}
void setup() {
  Serial.begin(115200);Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if(strcmp(info.product,"v3 P4 CCID")==0) {speed.store(info.speed);address.store(info.address);}
  });
  card.onApdu([](const uint8_t *data,size_t n,uint8_t *reply,size_t capacity) {
    if(n+2>capacity) return size_t(0);
    for(size_t i=0;i<n;++i) reply[i]=data[i]^0x31;
    reply[n]=0x90;reply[n+1]=0;++apdus;return n+2;
  });
  card.onEscape([](const uint8_t *data,size_t n,uint8_t *reply,size_t capacity) {
    if(n>capacity) return size_t(0);
    for(size_t i=0;i<n;++i) reply[i]=data[i]^0x57;
    ++escapes;return n;
  });
}
void loop() {
  if(!Serial.available()) {delay(1);return;}
  String command=Serial.readStringUntil('\n');command.trim();
  if(command.startsWith("start ")) {
    char controller[3]={};unsigned remove=1;
    sscanf(command.c_str(),"start %2s %u",controller,&remove);removable=remove;
    bool hs=strcmp(controller,"hs")==0;address.store(0);speed.store(255);apdus.store(0);escapes.store(0);
    EspUsbCcidConfig config;config.messageBytes=1024;config.removable=removable;config.name="P4 simulated card";
    bool ok=card.configure(config)==ESP_OK && card.insertCard(expectedAtr,sizeof(expectedAtr));
    EspUsbHostConfig hc;hc.port=hs?ESP_USB_HOST_PORT_FULL_SPEED:ESP_USB_HOST_PORT_HIGH_SPEED;
    hostStarted=host.begin(hc);ok &= hostStarted;
    EspUsbDeviceConfig dc;dc.product="v3 P4 CCID";dc.controller=hs?EspUsbController::HighSpeed:EspUsbController::FullSpeed;
    ok &= device.begin(dc);
    const uint32_t deadline=millis()+12000;
    while(ok && (!address.load() || !device.ready()) && int32_t(deadline-millis())>0) delay(1);
    ok &= address.load() && device.ready();
    if(ok) ok &= host.ccidOpen(address.load());
    Serial.printf("START ok=%u controller=%s removable=%u speed=%u capacity=%u error=%u\r\n",unsigned(ok),controller,unsigned(removable),speed.load(),unsigned(card.messageCapacity()),unsigned(device.lastError()));
  } else if(command=="exercise") {
    uint8_t atr[16];size_t atrBytes=0;
    bool ok=host.ccidPowerOn(atr,sizeof(atr),&atrBytes,ESP_USB_HOST_CCID_VOLTAGE_AUTO,0,address.load()) && atrBytes==sizeof(expectedAtr) && !memcmp(atr,expectedAtr,atrBytes) && card.cardPowered();
    Serial.printf("POWER ok=%u length=%u active=%u\r\n",unsigned(ok),unsigned(atrBytes),unsigned(card.cardPowered()));
    unsigned transfers=0,opaque=0,slots=0;
    const size_t lengths[]={4,52,63,64,65,116,129,500,513};
    for(size_t n:lengths) {
      if(!ok) break;
      for(size_t i=0;i<n;++i) sent[i]=uint8_t(i*37+n);
      size_t got=0;uint16_t status=0;
      ok &= host.ccidApdu(sent,n,received,sizeof(received),&got,&status,0,address.load()) && got==n && status==0x9000;
      for(size_t i=0;i<n && ok;++i) ok &= received[i]==uint8_t(sent[i]^0x31);
      if(ok) ++transfers;
    }
    for(size_t n:{1u,64u,513u}) {
      if(!ok) break;
      for(size_t i=0;i<n;++i) sent[i]=uint8_t(i*13+n);
      size_t got=0;ok &= host.ccidEscape(sent,n,received,sizeof(received),&got,0,address.load()) && got==n;
      for(size_t i=0;i<n && ok;++i) ok &= received[i]==uint8_t(sent[i]^0x57);
      if(ok) ++opaque;
    }
    ok &= host.ccidPowerOff(0,address.load()) && !card.cardPowered();
    if(removable) {
      EspUsbHostCcidStatus state;
      ok &= card.removeCard()==ESP_OK && host.ccidGetStatus(state,0,address.load()) && !state.present;
      if(ok) ++slots;
      ok &= card.insertCard(expectedAtr,sizeof(expectedAtr)) && host.ccidGetStatus(state,0,address.load()) && state.present && !state.active;
      if(ok) ++slots;
    } else {ok &= card.removeCard()==ESP_ERR_INVALID_STATE;if(ok) slots=1;}
    ok &= apdus.load()==9 && escapes.load()==3 && heap_caps_check_integrity_all(true);
    Serial.printf("CCID ok=%u apdu=%u escape=%u callbacks=%u/%u slots=%u\r\n",unsigned(ok),transfers,opaque,apdus.load(),escapes.load(),slots);
  } else if(command=="stop") Serial.println(stopUsb()?"STOPPED":"STOP_FAILED");
}
