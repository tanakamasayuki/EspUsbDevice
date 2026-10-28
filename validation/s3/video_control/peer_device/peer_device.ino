#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include <atomic>
#include "../../../../src/internal/EspUsbDeviceEndpoint.h"
#include "../../../../src/internal/EspUsbRuntime.h"
EspUsbDevice device;
EspUsbDeviceVideo camera(device,"MJPEG"), raw(device,"YUY2");
EspUsbDeviceVideo *video[2]={&camera,&raw};
uint8_t frame[65536], pixels[512];
alignas(64) uint8_t staging[256];
std::atomic<unsigned> results[2], successes[2], cancels[2], stops[2], bad[2], commits[2];
std::atomic<unsigned> transferErrors[2],unknownErrors[2];
uint64_t ticket[2]={};
// Private readiness probe: ensure the loan has reached an actual endpoint
// arm before interrupting it. The product API does not expose DMA internals.
bool armed(unsigned i) {
  const auto *d=device.configurationDescriptor(0);if(!d)return false;
  const unsigned bytes=d[2]|unsigned(d[3])<<8;unsigned interface=255;uint8_t endpoint=0;
  for(unsigned n=9;n+2<=bytes && d[n]>=2 && d[n]<=bytes-n;n+=d[n]) {
    if(d[n+1]==4 && d[n]==9)interface=d[n+2];
    if(d[n+1]==5 && d[n]==7 && interface==1+2*i && (d[n+2]&128))endpoint=d[n+2];
  }
  const uint8_t port=espusb::internal::usbRuntimeRhport();
  const uint32_t until=millis()+2000;
  while(endpoint && !espusb_device_edpt_busy(port,endpoint) && int32_t(until-millis())>0)delay(1);
  return endpoint && video[i]->frameInFlight() && espusb_device_edpt_busy(port,endpoint) && !espusb_device_edpt_stalled(port,endpoint);
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  for (unsigned i=0;i<sizeof(frame);++i) frame[i]=uint8_t(i*37+13);
  for (unsigned i=0;i<sizeof(pixels);++i) pixels[i]=uint8_t(i*17+7);
  memset(staging,0x5a,sizeof(staging));
  for (unsigned i=0;i<2;++i) {
    video[i]->onCommit([i](const EspUsbDeviceVideoCommit &){++commits[i];});
    video[i]->onFrameResult([i](const EspUsbTransferResult &r){
      if (!r.id || r.id!=ticket[i] || r.buffer!=(i ? pixels : frame) || video[i]->frameInFlight()) ++bad[i];
      if (r.status==EspUsbTransferStatus::Success) {++successes[i];if (!r.actualBytesKnown || r.actualBytes!=r.requestedBytes) ++bad[i];}
      else if (r.status==EspUsbTransferStatus::Cancelled) ++cancels[i];
      else if (r.status==EspUsbTransferStatus::Stopped) ++stops[i];
      else if (r.status==EspUsbTransferStatus::TransferError) {++transferErrors[i];if(!r.actualBytesKnown)++unknownErrors[i];}
      else ++bad[i];
      ++results[i];
    });
  }
}
void loop() {
  if (!Serial.available()) {delay(1);return;}
  String c=Serial.readStringUntil('\n');c.trim();
  if (c=="start") {
    if(device.ready()){Serial.println("STARTED 0");return;}
    // The standard plugin may reuse the running sketch between functions.
    // Explicit start owns test counters; stop has already retired all loans.
    for(unsigned i=0;i<2;++i){results[i]=successes[i]=cancels[i]=stops[i]=bad[i]=commits[i]=transferErrors[i]=unknownErrors[i]=0;ticket[i]=0;}
    EspUsbVideoConfig cfg;cfg.transport=EspUsbVideoTransport::Bulk;cfg.payloadBytes=65;
    cfg.payloadBuffer=staging+64;cfg.payloadBufferBytes=128;
    bool ok=camera.configure(cfg)==ESP_OK && camera.setFrameSize(16,16) && camera.setMaxFrameSize(65536);
    cfg.payloadBytes=128;cfg.payloadBuffer=nullptr;cfg.payloadBufferBytes=0;
    ok &= raw.configure(cfg)==ESP_OK && raw.setFrameSize(16,16) && raw.setFormat(EspUsbDeviceVideoFormat::Yuy2);
    ok &= device.begin() && camera.payloadBufferSize()==65 && raw.payloadBufferSize()==128;
    Serial.printf("STARTED %u\r\n",unsigned(ok));
  } else if (c.startsWith("queue ")) {
    unsigned i,n;sscanf(c.c_str(),"queue %u %u",&i,&n);
    esp_err_t e=i<2 ? video[i]->submitFrame(i ? pixels : frame,n,ticket[i]) : ESP_ERR_INVALID_ARG;
    Serial.printf("QUEUED %u %d\r\n",i,int(e));
  } else if(c.startsWith("armed "))Serial.printf("ARMED %u\r\n",unsigned(armed(c.substring(6).toInt())));
  else if(c.startsWith("ready ")) {
    unsigned i=c.substring(6).toInt();const uint32_t until=millis()+2000;
    while((!video[i]->streaming() || video[i]->frameInFlight()) && int32_t(until-millis())>0)delay(1);
    Serial.printf("READY %u\r\n",unsigned(video[i]->streaming() && !video[i]->frameInFlight()));
  } else if(c.startsWith("halted ")) {
    unsigned i=c.substring(7).toInt();const uint32_t until=millis()+2000;
    while(video[i]->frameInFlight() && int32_t(until-millis())>0)delay(1);
    uint64_t id=99;
    bool ok=!video[i]->frameInFlight() && !video[i]->streaming() &&
        video[i]->submitFrame(i ? pixels:frame,i ? 512:257,id)==ESP_ERR_INVALID_STATE && !id;
    Serial.printf("HALTED %u\r\n",unsigned(ok));
  } else if(c.startsWith("halt_stats ")) {
    unsigned i=0,expected=0;sscanf(c.c_str(),"halt_stats %u %u",&i,&expected);
    const uint32_t until=millis()+2000;
    while(results[i].load()<expected && int32_t(until-millis())>0)delay(1);
    Serial.printf("HALT_STATS %u results=%u success=%u cancel=%u error=%u unknown=%u bad=%u pending=%u\r\n",i,results[i].load(),successes[i].load(),cancels[i].load(),transferErrors[i].load(),unknownErrors[i].load(),bad[i].load(),unsigned(video[i]->frameInFlight()));
  } else if (c=="busy") {
    uint64_t id=99;
    bool ok=camera.submitFrame(frame,257,id)==ESP_ERR_INVALID_STATE && !id;
    ok &= camera.cancelFrame(ticket[1])==ESP_ERR_NOT_FOUND && raw.cancelFrame(ticket[0])==ESP_ERR_NOT_FOUND;
    Serial.printf("BUSY %u\r\n",unsigned(ok));
  } else if (c.startsWith("invalid ")) {
    unsigned i=c.substring(8).toInt();uint64_t id=99;
    bool ok=video[i]->submitFrame(i ? pixels : frame,i ? 511 : 65537,id)==ESP_ERR_INVALID_ARG && !id;
    id=99;ok &= video[i]->submitFrame(nullptr,1,id)==ESP_ERR_INVALID_ARG && !id;
    Serial.printf("INVALID %u\r\n",unsigned(ok));
  } else if (c.startsWith("cancel ")) {
    unsigned i=c.substring(7).toInt();esp_err_t e=video[i]->cancelFrame(ticket[i]);
    Serial.printf("CANCEL_REQUEST %d\r\n",int(e));
  } else if (c.startsWith("stats ")) {
    unsigned i=c.substring(6).toInt();auto v=video[i]->commit();
    Serial.printf("STATS %u results=%u success=%u cancel=%u stopped=%u bad=%u pending=%u commits=%u payload=%u\r\n",i,
      results[i].load(),successes[i].load(),cancels[i].load(),stops[i].load(),bad[i].load(),unsigned(video[i]->frameInFlight()),commits[i].load(),unsigned(v.maxPayloadTransferSize));
  } else if (c=="stop") {
    bool ok=device.end()==ESP_OK && !camera.payloadBufferSize() && !raw.payloadBufferSize() && heap_caps_check_integrity_all(true);
    for (unsigned i=0;i<64;++i) ok &= staging[i]==0x5a && staging[192+i]==0x5a;
    Serial.println(ok ? "STOPPED" : "STOP_FAILED");
  }
}
