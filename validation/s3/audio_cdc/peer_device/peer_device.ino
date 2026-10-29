#include <Arduino.h>
#include <EspUsbDevice.h>
#include <algorithm>
#include "../../../../src/internal/EspUsbDwc2Hardware.h"
EspUsbDevice device;
EspUsbAudioFunction *audio=nullptr;
EspUsbAudioPlaybackStream *playback=nullptr;
EspUsbAudioCaptureStream *capture=nullptr;
EspUsbDeviceCdcSerial *serialPort=nullptr;
unsigned pcmBytes=0,pcmBad=0,cdcRx=0,cdcTx=0;
uint8_t samples[256];
void pump() {
  if(playback) {
    uint8_t data[512]; const size_t n=playback->read(data,sizeof(data)); pcmBytes+=n;
    for(size_t i=0;i<n;++i)if(data[i]!=0x5a)++pcmBad;
  }
  if(capture)capture->write(samples,std::min(sizeof(samples),capture->availableForWrite()));
  if(serialPort) {
    uint8_t data[64];const size_t n=serialPort->read(data,std::min(sizeof(data),serialPort->writeAvailable()));
    if(n){cdcRx+=n;cdcTx+=serialPort->write(data,n);}
  }
}
void setup(){Serial.begin(115200);Serial.setTimeout(100);memset(samples,0x5a,sizeof(samples));}
void loop() {
  pump();
  if(!Serial.available()){delay(1);return;}
  String s=Serial.readStringUntil('\n');s.trim();
  if(s.startsWith("start ")) {
    unsigned protocol,order,rate;sscanf(s.c_str(),"start %u %u %u",&protocol,&order,&rate);
    if(device.started()){Serial.println("STARTED 0");return;}
    delete audio;delete serialPort;audio=nullptr;serialPort=nullptr;playback=nullptr;capture=nullptr;
    if(order)serialPort=new EspUsbDeviceCdcSerial(device,"console");
    audio=new EspUsbAudioFunction(device,protocol==1 ? EspUsbAudioProtocol::Uac1:EspUsbAudioProtocol::Uac2);
    if(!order)serialPort=new EspUsbDeviceCdcSerial(device,"console");
    playback=&audio->addPlaybackStream();capture=&audio->addCaptureStream();
    bool ok=playback->addFormat({rate,2,2,16}) && capture->addFormat({rate,2,2,16});
    EspUsbAudioConfig ac;ac.eventCapacity=0;ac.playbackBufferFrames=ac.captureBufferFrames=65;
    EspUsbBulkStreamConfig cc;cc.rxCapacity=97;cc.txCapacity=131;cc.transferBytes=64;
    ok &= audio->configure(ac)==ESP_OK && serialPort->configure(cc)==ESP_OK;
    pcmBytes=pcmBad=cdcRx=cdcTx=0;
    EspUsbDeviceConfig config;config.product="v3 Audio CDC composite";
    ok &= device.begin(config);
    Serial.printf("STARTED %u error=%d\r\n",unsigned(ok),int(device.lastError()));
  } else if(s=="stats") {
    Serial.printf("CDC_STATE mounted=%u connected=%u available=%u writable=%u\r\n",unsigned(serialPort->mounted()),unsigned(serialPort->connected()),unsigned(serialPort->available()),unsigned(serialPort->writeAvailable()));
    const auto *r=espusb_dwc2_registers(0);
    for(unsigned ep=1;ep<7;++ep)if(r->epin[ep].ctl&(1u<<15))
      Serial.printf("TX_FIFO ep=%u fifo=%u\r\n",ep,unsigned((r->epin[ep].ctl>>22)&15));
    const auto rx=playback->stats(),tx=capture->stats();
    Serial.printf("COMPOSITE rx=%u tx=%u consumed=%u bad=%u cdcRx=%u cdcTx=%u baud=%u\r\n",unsigned(rx.transferredBytes),unsigned(tx.transferredBytes),pcmBytes,pcmBad,cdcRx,cdcTx,unsigned(serialPort->lineCoding().baud));
  } else if(s=="stop")Serial.println(device.end()==ESP_OK ? "STOPPED":"STOP_FAILED");
}
