#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include <atomic>
EspUsbDevice device;
EspUsbDeviceVendor first(device);
EspUsbDeviceBulkStream *stream[2]={&first,nullptr};
EspUsbDeviceCdcSerial *serialPort=nullptr;
alignas(64) uint8_t tx[2][256],rx[2][320];
struct Counts {
  std::atomic<unsigned> tx{0},rx{0},bad{0};
  std::atomic<bool> txPending{false},rxPending{false};
  EspUsbTransferStatus txStatus=EspUsbTransferStatus::Stopped,rxStatus=EspUsbTransferStatus::Stopped;
  uint64_t txId=0,rxId=0;size_t txLength=0;
} counts[2];
const char *name(EspUsbTransferStatus s) {
  switch(s){case EspUsbTransferStatus::Success:return "Success";case EspUsbTransferStatus::Cancelled:return "Cancelled";
    case EspUsbTransferStatus::TransferError:return "TransferError";case EspUsbTransferStatus::BusReset:return "BusReset";default:return "Stopped";}
}
void callbacks(unsigned i) {
  stream[i]->onDirectTxComplete([i](const EspUsbTransferResult &r){
    auto &c=counts[i];if(!c.txPending.exchange(false) || r.buffer!=tx[i] || r.id!=c.txId || r.requestedBytes!=c.txLength)++c.bad;
    if(r.status==EspUsbTransferStatus::Success && (!r.actualBytesKnown || r.actualBytes!=c.txLength))++c.bad;
    c.txStatus=r.status;++c.tx;
    Serial.printf("TX_RESULT i=%u status=%s actual=%u known=%u\r\n",i,name(r.status),unsigned(r.actualBytes),unsigned(r.actualBytesKnown));
  });
  stream[i]->onDirectRxComplete([i](const EspUsbTransferResult &r){
    auto &c=counts[i];if(!c.rxPending.exchange(false) || r.buffer!=rx[i]+64 || r.id!=c.rxId || r.requestedBytes!=192)++c.bad;
    if(r.status==EspUsbTransferStatus::Success){
      if(!r.actualBytesKnown || r.actualBytes>192)++c.bad;
      for(unsigned n=0;n<r.actualBytes;++n)if(rx[i][64+n]!=uint8_t(n*17+7+i*53))++c.bad;
    }
    for(unsigned n=0;n<64;++n)if(rx[i][n]!=0x5a || rx[i][256+n]!=0x5a)++c.bad;
    c.rxStatus=r.status;++c.rx;
  });
}
void setup(){Serial.begin(115200);Serial.setTimeout(100);}
void loop(){
  if(!Serial.available()){delay(1);return;}String s=Serial.readStringUntil('\n');s.trim();
  if(s.startsWith("start ")) {
    unsigned kind,copy;sscanf(s.c_str(),"start %u %u",&kind,&copy);
    delete stream[1];serialPort=nullptr;
    if(kind)stream[1]=serialPort=new EspUsbDeviceCdcSerial(device);else stream[1]=new EspUsbDeviceVendor(device);
    bool ok=true;
    for(unsigned i=0;i<2;++i){
      auto &c=counts[i];c.tx=c.rx=c.bad=0;c.txPending=c.rxPending=false;c.txStatus=c.rxStatus=EspUsbTransferStatus::Stopped;
      memset(rx[i],0x5a,sizeof(rx[i]));for(unsigned n=0;n<sizeof(tx[i]);++n)tx[i][n]=uint8_t(n*37+13+i*41);
      callbacks(i);EspUsbBulkStreamConfig cfg;
      if(!copy){cfg.rxMode=EspUsbBulkRxMode::Borrowed;cfg.rxCapacity=cfg.txCapacity=cfg.transferBytes=0;}
      else {cfg.rxCapacity=97;cfg.txCapacity=131;cfg.transferBytes=128;}
      ok &= stream[i]->configure(cfg)==ESP_OK;
    }
    ok &= device.begin();Serial.printf("STARTED %u\r\n",unsigned(ok));
  } else if(s.startsWith("queue ")) {
    unsigned i;char direction[4];sscanf(s.c_str(),"queue %u %3s",&i,direction);bool ok=i<2;
    if(ok && String(direction)!="tx") {
      auto &c=counts[i];memset(rx[i]+64,0x5a,192);c.rxPending=true;
      auto r=stream[i]->readDirect(rx[i]+64,192);c.rxId=r.id;ok &= bool(r);if(!r)c.rxPending=false;
    }
    if(ok && String(direction)!="rx") {
      auto &c=counts[i];c.txLength=129;c.txPending=true;
      auto r=stream[i]->writeDirect(tx[i],129);c.txId=r.id;ok &= bool(r);if(!r)c.txPending=false;
    }
    Serial.printf("QUEUED %u\r\n",unsigned(ok));
  } else if(s.startsWith("stats ")) {
    unsigned i,txCount,rxCount;sscanf(s.c_str(),"stats %u %u %u",&i,&txCount,&rxCount);auto &c=counts[i];
    uint32_t until=millis()+3000;while((c.tx.load()<txCount || c.rx.load()<rxCount) && int32_t(until-millis())>0)delay(1);
    Serial.printf("STATS %u tx=%u rx=%u txStatus=%s rxStatus=%s txPending=%u rxPending=%u bad=%u\r\n",i,c.tx.load(),c.rx.load(),name(c.txStatus),name(c.rxStatus),unsigned(c.txPending.load()),unsigned(c.rxPending.load()),c.bad.load());
  } else if(s=="blocked") {
    delay(20); // Verify the HALT state after its asynchronous fence has retired.
    Serial.printf("BLOCKED %u\r\n",unsigned(!stream[0]->writeAvailable() && !stream[0]->write(tx[0],1)));
  } else if(s.startsWith("copytx ")) {
    unsigned i,n;sscanf(s.c_str(),"copytx %u %u",&i,&n);Serial.printf("COPIED %u\r\n",unsigned(stream[i]->write(tx[i],n)==n));
  } else if(s.startsWith("copyrx ")) {
    unsigned i,n;sscanf(s.c_str(),"copyrx %u %u",&i,&n);uint8_t data[192];
    uint32_t until=millis()+3000;while(stream[i]->available()<int(n) && int32_t(until-millis())>0)delay(1);
    bool ok=stream[i]->available()==int(n);
    if(ok && n){ok=stream[i]->read(data,n)==n;for(unsigned j=0;j<n;++j)ok &= data[j]==uint8_t(j*17+7+i*53);}
    Serial.printf("COPY_RX %u\r\n",unsigned(ok));
  } else if(s=="cdc") {
    const auto c=serialPort ? serialPort->lineCoding() : EspUsbDeviceCdcLineCoding{};
    Serial.printf("CDC %u\r\n",unsigned(serialPort && serialPort->lineState().dtr && c.baud==115200));
  } else if(s=="stop")Serial.println(device.end()==ESP_OK && heap_caps_check_integrity_all(true) ? "STOPPED" : "STOP_FAILED");
}
