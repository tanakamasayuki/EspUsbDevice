#include <Arduino.h>
#include <EspUsbDevice.h>
#include <algorithm>
#include "../../../../src/internal/EspUsbDwc2Hardware.h"
EspUsbDevice device;
EspUsbAudioFunction *audio = nullptr;
EspUsbAudioPlaybackStream *playback = nullptr;
EspUsbAudioCaptureStream *capture = nullptr;
uint8_t rx[1026], tx[1026], samples[512];
size_t externalRingBytes = 0;
bool ringGuards() {
  if (!externalRingBytes) return true;
  if (rx[0] != 0xc3 || tx[0] != 0xc3) return false;
  for (size_t i = externalRingBytes + 1; i < sizeof(rx); ++i)
    if (rx[i] != 0xc3 || tx[i] != 0xc3) return false;
  return true;
}
uint32_t received = 0, bad = 0;
bool feeding = true;
unsigned activeDirections = 0, activeRate = 0;
uint32_t suspendedRx = 0, suspendedTx = 0;
void setup() { Serial.begin(115200); Serial.setTimeout(100); memset(samples, 0x5a, sizeof(samples)); }
void pump() {
  if (playback) {
    uint8_t data[512]; size_t n = playback->read(data, sizeof(data)); received += n;
    for (size_t i = 0; i < n; ++i) if (data[i] != 0x5a) ++bad;
  }
  if (capture && feeding) capture->write(samples, std::min(sizeof(samples), capture->availableForWrite()));
}
void loop() {
  pump();
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ")) {
    unsigned protocol, directions, rate, frames, external;
    sscanf(command.c_str(), "start %u %u %u %u %u", &protocol, &directions, &rate, &frames, &external);
    const auto selected = protocol == 1 ? EspUsbAudioProtocol::Uac1 : EspUsbAudioProtocol::Uac2;
    const bool reused = audio && audio->protocol() == selected && activeDirections == directions;
    const unsigned oldRate = activeRate;
    bool ok = true;
    if (reused) {
      ok &= !playback || playback->clearFormats() == ESP_OK;
      ok &= !capture || capture->clearFormats() == ESP_OK;
      ok &= !audio->currentSampleRate();
      Serial.printf("RECONFIGURED %u from=%u to=%u\r\n", unsigned(ok), oldRate, rate);
    } else {
      delete audio; audio = new EspUsbAudioFunction(device, selected);
      playback = nullptr; capture = nullptr;
    }
    activeDirections = directions; activeRate = rate;
    if (directions & 1) { playback = &audio->addPlaybackStream(); ok &= playback->addFormat({rate,2,2,16}); }
    if (directions & 2) { capture = &audio->addCaptureStream(); ok &= capture->addFormat({rate,2,2,16}); }
    if (command.endsWith(" multi")) {
      if (playback) ok &= playback->addFormat({44100,2,2,16});
      if (capture) ok &= capture->addFormat({44100,2,2,16});
      for (unsigned r = 8000; r <= 18000; r += 1000) {
        if (playback) ok &= playback->addFormat({r,2,2,16});
        if (capture) ok &= capture->addFormat({r,2,2,16});
      }
    }
    const bool alternates = command.endsWith(" alternates");
    if (alternates) {
      // Interleaved registration must retain PCM alternate order, not rate order.
      for (unsigned r : {24000u,16000u}) for (unsigned bytes : {2u,3u,4u}) {
        if (r == rate && bytes == 2) continue;
        if (playback) ok &= playback->addFormat({r,2,uint8_t(bytes),uint8_t(bytes*8)});
        if (capture) ok &= capture->addFormat({r,2,uint8_t(bytes),uint8_t(bytes*8)});
      }
    }
    if (alternates) for (unsigned r : {24000u,16000u}) {
      if (playback) ok &= playback->addFormat({r,2,4,24});
      if (capture) ok &= capture->addFormat({r,2,4,24});
    }
    EspUsbAudioConfig config; config.eventCapacity = 0; config.playbackBufferFrames = config.captureBufferFrames = frames;
    externalRingBytes = external ? frames * (alternates ? 8 : 4) : 0;
    if (externalRingBytes > sizeof(rx) - 2) { Serial.println("STARTED 0"); return; }
    memset(rx, 0xc3, sizeof(rx)); memset(tx, 0xc3, sizeof(tx));
    if (external) {
      config.playbackBuffer = rx + 1; config.captureBuffer = tx + 1;
      config.playbackBufferBytes = sizeof(rx) - 2; config.captureBufferBytes = sizeof(tx) - 2;
    }
    ok &= audio->configure(config) == ESP_OK && device.begin();
    ok &= (!playback || playback->capacity() == frames * (alternates ? 8 : 4)) && (!capture || capture->capacity() == frames * (alternates ? 8 : 4));
    if (capture) { ok &= capture->write(samples, 3) == 0; capture->clearBuffer(); }
    feeding = true; received = bad = 0;
    Serial.printf("STARTED %u\r\n", unsigned(ok));
  } else if (command.startsWith("formats ")) {
    unsigned bits,rate,bytes; sscanf(command.c_str(),"formats %u %u %u",&bits,&rate,&bytes);
    EspUsbAudioFormat a{},b{}; bool ok = playback && capture && playback->currentFormat(a) && capture->currentFormat(b);
    const unsigned frame = 2*bytes, capacity = 520/frame*frame;
    ok &= a.sampleRate == rate && b.sampleRate == rate && a.channels == 2 && b.channels == 2 && a.bitsPerSample == bits && b.bitsPerSample == bits && a.bytesPerSample == bytes && b.bytesPerSample == bytes;
    ok &= playback->capacity() == capacity && capture->capacity() == capacity && capture->availableForWrite() % frame == 0;
    Serial.printf("PCM_FORMAT ok=%u bits=%u rate=%u capacity=%u\r\n",unsigned(ok),bits,rate,unsigned(playback->capacity()));
  } else if (command == "guards") {
    Serial.printf("RING_GUARDS ok=%u external=%u bytes=%u\r\n", unsigned(ringGuards()), unsigned(externalRingBytes != 0), unsigned(externalRingBytes));
  } else if (command == "formats_off") {
    bool ok = false; const uint32_t until = millis()+3000;
    do {
      EspUsbAudioFormat a{123,1,2,16},b=a;
      ok = !playback->currentFormat(a) && !capture->currentFormat(b) && a.sampleRate == 123 && b.sampleRate == 123;
      if (!ok) delay(1);
    } while (!ok && int32_t(until-millis())>0);
    Serial.printf("FORMATS_OFF %u\r\n",unsigned(ok));
  } else if (command == "poison") {
    uint8_t data[512]; memset(data,0xa5,sizeof(data));
    bool ok = capture && capture->clearBuffer() && capture->write(data,sizeof(data)) > 0;
    Serial.printf("POISONED %u\r\n",unsigned(ok));
  } else if (command == "iso_diag") {
    const auto *r = espusb_dwc2_registers(0);
    Serial.printf("ISO_DIAG sts=%08x mask=%08x outctl=%08x outsize=%08x outint=%08x\r\n",unsigned(r->gintsts),unsigned(r->gintmsk),unsigned(r->epout[1].ctl),unsigned(r->epout[1].tsiz),unsigned(r->epout[1].intr));
  } else if (command == "clock") {
    Serial.printf("CLOCK %u\r\n", unsigned(audio->currentSampleRate()));
  } else if (command == "resetstats") {
    if (playback) playback->resetStats(); if (capture) capture->resetStats(); received = bad = 0;
    Serial.println("STATS_RESET");
  } else if (command == "locked") {
    bool ok = !playback || playback->clearFormats() == ESP_ERR_INVALID_STATE;
    ok &= !capture || capture->clearFormats() == ESP_ERR_INVALID_STATE;
    ok &= device.ready() && audio->currentSampleRate() == activeRate;
    Serial.printf("FORMATS_LOCKED %u\r\n", unsigned(ok));
  } else if (command == "silence") { feeding = false; if (capture) capture->clearBuffer(); Serial.println("SILENCE");
  } else if (command == "stats") {
    auto a = playback ? playback->stats() : EspUsbAudioStreamStats{};
    auto b = capture ? capture->stats() : EspUsbAudioStreamStats{};
    Serial.printf("STATS received=%u bad=%u rx=%u tx=%u underrun=%u\r\n", received, bad, a.transferredBytes, b.transferredBytes, b.underrunBytes);
  } else if (command == "suspended") {
    const uint32_t until = millis() + 3000;
    while (!device.suspended() && int32_t(until - millis()) > 0) delay(1);
    suspendedRx = playback ? playback->stats().transferredBytes : 0;
    suspendedTx = capture ? capture->stats().transferredBytes : 0; delay(100);
    bool ok = device.started() && device.ready() && device.suspended() && audio->currentSampleRate() == activeRate;
    ok &= !playback || playback->stats().transferredBytes == suspendedRx;
    ok &= !capture || capture->stats().transferredBytes == suspendedTx;
    Serial.printf("ISO_STATE_SUSPENDED %u\r\n", unsigned(ok));
  } else if (command == "resumed") {
    const uint32_t rxBytes = playback ? playback->stats().transferredBytes - suspendedRx : 0;
    const uint32_t txBytes = capture ? capture->stats().transferredBytes - suspendedTx : 0;
    bool ok = device.started() && device.ready() && !device.suspended() && audio->currentSampleRate() == activeRate && !bad;
    ok &= !(activeDirections & 1) || rxBytes > activeRate * 4 * 13 / 10;
    ok &= !(activeDirections & 2) || txBytes > activeRate * 4 * 13 / 10;
    Serial.printf("ISO_STATE_RESUMED ok=%u rx=%u tx=%u bad=%u\r\n", unsigned(ok), unsigned(rxBytes), unsigned(txBytes), unsigned(bad));
  } else if (command == "stop") { Serial.println(device.end() == ESP_OK ? "STOPPED" : "STOP_FAILED"); }
}
