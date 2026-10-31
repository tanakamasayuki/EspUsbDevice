#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include <atomic>
#include "../../common/probe_link.h"
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
alignas(64) uint8_t tx[192],rx[PROBE_BULK_MPS+128];
std::atomic<unsigned> returned{0},bad{0};
std::atomic<bool> txPending{false},rxPending{false};
uint64_t txId=0,rxId=0;
std::atomic<EspUsbTransferStatus> expected=EspUsbTransferStatus::Success;
void result(const EspUsbTransferResult &r,bool input) {
  if(!(input ? txPending:rxPending).exchange(false) || r.id!=(input ? txId:rxId) ||
     r.buffer!=(input ? tx+64:rx+64) || r.requestedBytes!=(input ? 64:PROBE_BULK_MPS) || r.status!=expected || r.actualBytes>64)++bad;
  if(expected==EspUsbTransferStatus::Success) {
    if(!r.actualBytesKnown || r.actualBytes!=64)++bad;
    for(unsigned i=0;!input && i<64;++i)if(rx[64+i]!=uint8_t(i*17+7))++bad;
  }
  for(unsigned i=0;i<64;++i)if(tx[i]!=0xa5 || tx[128+i]!=0xa5 || rx[i]!=0x5a || rx[64+PROBE_BULK_MPS+i]!=0x5a)++bad;
  ++returned;
}
void setup() {
  Serial.begin(115200);Serial.setTimeout(100);
  vendor.onDirectTxComplete([](const EspUsbTransferResult &r){result(r,true);});
  vendor.onDirectRxComplete([](const EspUsbTransferResult &r){result(r,false);});
}
void loop() {
  if(!Serial.available()){delay(1);return;}String s=Serial.readStringUntil('\n');s.trim();
  if(s.startsWith("start ")) {
    returned=bad=0;txPending=rxPending=false;
    EspUsbBulkStreamConfig cfg;cfg.rxMode=EspUsbBulkRxMode::Borrowed;cfg.rxCapacity=cfg.txCapacity=cfg.transferBytes=0;
    EspUsbDeviceConfig identity;identity.eventQueueDepth=s.substring(6).toInt();
    bool ok=!device.suspended() && vendor.configure(cfg)==ESP_OK && device.begin(identity);
    Serial.printf("STARTED %u\r\n",unsigned(ok && device.started() && !device.suspended()));
  } else if(s.startsWith("state ")) {
    bool suspended=s.substring(6).toInt();uint32_t until=millis()+3000;
    while(device.suspended()!=suspended && int32_t(until-millis())>0)delay(1);
    Serial.printf("STATE started=%u ready=%u suspended=%u\r\n",unsigned(device.started()),unsigned(device.ready()),unsigned(device.suspended()));
  } else if(s=="queue") {
    bool ok=!txPending.load() && !rxPending.load();expected=EspUsbTransferStatus::Success;
    memset(tx,0xa5,sizeof(tx));memset(rx,0x5a,sizeof(rx));for(unsigned i=0;i<64;++i)tx[64+i]=uint8_t(i*37+13);
    txPending=rxPending=true;
    auto t=vendor.writeDirect(tx+64,64);txId=t.id;if(!t)txPending=false;
    auto r=vendor.readDirect(rx+64,PROBE_BULK_MPS);rxId=r.id;if(!r)rxPending=false;
    Serial.printf("QUEUED %u\r\n",unsigned(ok && bool(t) && bool(r)));
  } else if(s=="cancel") {
    expected=EspUsbTransferStatus::Cancelled;
    bool ok=vendor.cancelDirectTx(txId)==ESP_OK && vendor.cancelDirectRx(rxId)==ESP_OK;
    Serial.printf("CANCELLED %u\r\n",unsigned(ok));
  } else if(s.startsWith("quiet ") || s.startsWith("result ")) {
    unsigned target=s.substring(s.indexOf(' ')+1).toInt();uint32_t until=millis()+3000;
    if(s.startsWith("quiet "))delay(100);
    else while(returned.load()<target && int32_t(until-millis())>0)delay(1);
    Serial.printf("RESULT count=%u expected=%u txPending=%u rxPending=%u bad=%u\r\n",returned.load(),target,unsigned(txPending.load()),unsigned(rxPending.load()),bad.load());
  } else if(s=="metadata") {
    EspUsbDeviceConfig c;c.startMode=EspUsbDeviceStartMode::DescriptorsOnly;
    bool ok=device.begin(c) && device.started() && !device.ready() && !device.suspended();
    ok &= device.end()==ESP_OK && !device.started() && !device.ready() && !device.suspended();
    Serial.printf("METADATA %u\r\n",unsigned(ok));
  } else if(s=="stop") {
    expected=EspUsbTransferStatus::Stopped;
    bool ok=device.end()==ESP_OK && !device.started() && !device.ready() && !device.suspended() && heap_caps_check_integrity_all(true);
    Serial.println(ok ? "STOPPED":"STOP_FAILED");
  }
}
