#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include <atomic>
EspUsbDevice device;
EspUsbDeviceCdcSerial console(device);
char text[4098];
std::atomic<bool> ended{false},endOk{false};
std::atomic<unsigned> workerBytes{0},workerMs{0},workerRuns{0};
void stopLater(void *) {
  delay(30);endOk=device.end()==ESP_OK;ended=true;vTaskDelete(nullptr);
}
void setup() {
  Serial.begin(115200);Serial.setTimeout(100);
  for(size_t i=0;i<sizeof(text)-1;++i)text[i]='A'+i%26;text[sizeof(text)-1]=0;
  console.onRxData([](const uint8_t *,size_t){
    const uint32_t at=millis();workerBytes=console.write(reinterpret_cast<const uint8_t *>(text),1025);
    console.flush();workerMs=millis()-at;++workerRuns;
  });
}
void loop() {
  if(!Serial.available()){delay(1);return;}
  String c=Serial.readStringUntil('\n');c.trim();
  if(c.startsWith("start ")) {
    const bool disabled=c=="start disabled";
    EspUsbBulkStreamConfig config;config.rxMode=EspUsbBulkRxMode::Callback;config.rxCapacity=0;
    config.txCapacity=disabled?0:64;config.transferBytes=disabled?0:64;config.autoFlush=c.substring(6).toInt();
    bool ok=console.configure(config)==ESP_OK;console.setTimeout(25);console.setTxTimeoutMs(1000);
    workerBytes=workerMs=workerRuns=0;
    EspUsbDeviceConfig dc;dc.product="v3 CDC TX deadlines";ok &= device.begin(dc);
    Serial.printf("STARTED %u\r\n",unsigned(ok && console.writeCapacity()==(disabled?0:64) && console.txTimeoutMs()==1000 && console.getTimeout()==25));
  } else if(c=="disabled") {
    const uint32_t at=millis();const size_t n=console.write(reinterpret_cast<const uint8_t *>(text),1025);console.flush();
    const unsigned elapsed=millis()-at;
    Serial.printf("DISABLED ok=%u elapsed=%u\r\n",unsigned(!n && elapsed<50 && console.txTimeoutMs()==1000),elapsed);
  } else if(c=="print") {
    console.setTxTimeoutMs(3000);const size_t n=console.print(text);console.flush();
    Serial.printf("PRINT ok=%u bytes=%u free=%u dtr=%u\r\n",unsigned(n==4097 && console.writeAvailable()==64),unsigned(n),unsigned(console.writeAvailable()),unsigned(console.connected()));
  } else if(c.startsWith("blocked ")) {
    const unsigned timeout=c.substring(8).toInt();console.setTxTimeoutMs(timeout);
    const uint32_t at=millis();const size_t n=console.write(reinterpret_cast<const uint8_t *>(text),513);const unsigned elapsed=millis()-at;
    const uint32_t f=millis();console.flush();const unsigned flushed=millis()-f;
    const unsigned expected=timeout?128:64; // One DMA packet plus one ring, or one immediate write.
    bool ok=n==expected && console.writeAvailable()==(timeout?0:64) && elapsed<(timeout+150) && flushed<(timeout+150);
    if(timeout)ok &= elapsed>=timeout && flushed>=timeout;
    Serial.printf("BLOCKED ok=%u bytes=%u elapsed=%u flush=%u\r\n",unsigned(ok),unsigned(n),elapsed,flushed);
  } else if(c=="dmawait") {
    console.setTxTimeoutMs(60);const bool empty=console.writeAvailable()==64;
    const uint32_t at=millis();console.flush();const unsigned elapsed=millis()-at;
    Serial.printf("DMA_WAIT ok=%u elapsed=%u\r\n",unsigned(empty && elapsed>=60 && elapsed<210),elapsed);
  } else if(c=="flushed") {
    console.setTxTimeoutMs(1000);console.flush();
    Serial.printf("FLUSHED %u\r\n",unsigned(console.writeAvailable()==64));
  } else if(c=="worker") {
    const uint32_t until=millis()+2000;while(!workerRuns.load() && int32_t(until-millis())>0)delay(1);
    bool ok=workerRuns==1 && workerBytes==64 && workerMs<50;
    Serial.printf("WORKER ok=%u bytes=%u elapsed=%u\r\n",unsigned(ok),workerBytes.load(),workerMs.load());
  } else if(c=="stopduring") {
    ended=false;endOk=false;console.setTxTimeoutMs(1000);
    bool ok=xTaskCreate(stopLater,"cdc-stop",4096,nullptr,2,nullptr)==pdPASS;
    const uint32_t at=millis();const size_t n=console.write(reinterpret_cast<const uint8_t *>(text),1025);const unsigned elapsed=millis()-at;
    const uint32_t until=millis()+5000;while(!ended.load() && int32_t(until-millis())>0)delay(1);
    ok &= n==128 && elapsed<300 && ended && endOk && !device.started() && !console.writeCapacity() && console.txTimeoutMs()==1000 && heap_caps_check_integrity_all(true);
    Serial.printf("STOP_DURING ok=%u bytes=%u elapsed=%u\r\n",unsigned(ok),unsigned(n),elapsed);
  } else if(c=="stop") Serial.println(device.end()==ESP_OK && heap_caps_check_integrity_all(true)?"STOPPED":"STOP_FAILED");
}
