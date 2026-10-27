#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
EspUsbDevice device;
EspUsbDeviceCcid first(device),second(device);
EspUsbDeviceCcid *reader[2]={&first,&second};
const uint8_t atrValue[]={0x3b,0x80,0x01,0x81};
bool metadata() {
  bool ok=true;
  for(size_t capacity:{64u,271u,320u,1024u,65535u}) {
    EspUsbDevice local;EspUsbDeviceCcid card(local);EspUsbCcidConfig cfg;cfg.messageBytes=capacity;
    ok &= card.configure(cfg)==ESP_OK;
    auto invalid=cfg;invalid.messageBytes=63;ok &= card.configure(invalid)==ESP_ERR_INVALID_SIZE && card.configuration().messageBytes==capacity;
    EspUsbDeviceConfig identity;identity.startTinyUsb=false;
    ok &= local.begin(identity) && card.messageCapacity()==capacity;
    ok &= card.configure(cfg)==ESP_ERR_INVALID_STATE && card.onApdu(nullptr)==ESP_ERR_INVALID_STATE;
    ok &= local.end()==ESP_OK && !card.messageCapacity();
  }
  {
    EspUsbDevice local;EspUsbDeviceCcid card(local);EspUsbCcidConfig cfg;cfg.removable=false;cfg.messageBytes=64;
    ok &= card.configure(cfg)==ESP_OK;EspUsbDeviceConfig identity;identity.startTinyUsb=false;
    ok &= !local.begin(identity) && local.lastError()==ESP_ERR_INVALID_STATE && local.end()==ESP_OK;
    ok &= card.insertCard(atrValue,sizeof(atrValue)) && local.begin(identity);
    ok &= !card.insertCard(atrValue,sizeof(atrValue)) && card.removeCard()==ESP_ERR_INVALID_STATE;
    ok &= local.end()==ESP_OK && card.removeCard()==ESP_OK;
  }
  return ok;
}
void setup(){Serial.begin(115200);Serial.setTimeout(100);}
void loop(){
  if(!Serial.available()){delay(1);return;}String s=Serial.readStringUntil('\n');s.trim();
  if(s=="metadata") {
    bool ok=metadata();delay(10);size_t before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    for(unsigned i=0;i<3 && ok;++i)ok &= metadata();delay(10);
    Serial.printf("METADATA ok=%u before=%u after=%u\r\n",unsigned(ok),unsigned(before),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  } else if(s.startsWith("start ")) {
    unsigned bytes,removable;sscanf(s.c_str(),"start %u %u",&bytes,&removable);bool ok=true;
    for(unsigned i=0;i<2;++i) {
      EspUsbCcidConfig cfg;cfg.messageBytes=bytes+i*64;cfg.removable=removable;cfg.name=i ? "Reader B":"Reader A";
      ok &= reader[i]->configure(cfg)==ESP_OK && reader[i]->insertCard(atrValue,sizeof(atrValue));
      ok &= reader[i]->onApdu([i](const uint8_t *data,size_t n,uint8_t *reply,size_t capacity){
        if(n>capacity)return size_t(0);for(size_t j=0;j<n;++j)reply[j]=data[j]^uint8_t(0x31+i);return n;
      })==ESP_OK;
      ok &= reader[i]->onEscape([i](const uint8_t *data,size_t n,uint8_t *reply,size_t capacity){
        if(n>capacity)return size_t(0);for(size_t j=0;j<n;++j)reply[j]=data[j]^uint8_t(0x31+i);return n;
      })==ESP_OK;
    }
    ok &= device.begin();Serial.printf("STARTED %u\r\n",unsigned(ok));
  } else if(s.startsWith("remove ")) {
    unsigned i=s.substring(7).toInt();Serial.printf("REMOVED %u\r\n",unsigned(i<2 && reader[i]->removeCard()==ESP_OK));
  } else if(s.startsWith("insert ")) {
    unsigned i=s.substring(7).toInt();Serial.printf("INSERTED %u\r\n",unsigned(i<2 && reader[i]->insertCard(atrValue,sizeof(atrValue))));

  } else if(s=="stop")Serial.println(device.end()==ESP_OK && !first.messageCapacity() && !second.messageCapacity() && heap_caps_check_integrity_all(true) ? "STOPPED":"STOP_FAILED");
}
