#include <Arduino.h>
#include <EspUsbDevice.h>
#include <algorithm>
EspUsbDevice device;
EspUsbAudioFunction *audio = nullptr;
EspUsbAudioPlaybackStream *playback = nullptr;
EspUsbAudioCaptureStream *capture = nullptr;
uint8_t rx[1025], tx[1025], samples[512];
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
    activeDirections = directions; activeRate = rate;
    delete audio; audio = new EspUsbAudioFunction(device, protocol == 1 ? EspUsbAudioProtocol::Uac1 : EspUsbAudioProtocol::Uac2);
    playback = nullptr; capture = nullptr; bool ok = true;
    if (directions & 1) { playback = &audio->addPlaybackStream(); ok &= playback->addFormat({rate,2,2,16}); }
    if (directions & 2) { capture = &audio->addCaptureStream(); ok &= capture->addFormat({rate,2,2,16}); }
    EspUsbAudioConfig config; config.eventCapacity = 0; config.playbackBufferFrames = config.captureBufferFrames = frames;
    if (external) { config.playbackBuffer = rx + 1; config.captureBuffer = tx + 1; }
    ok &= audio->configure(config) == ESP_OK && device.begin();
    ok &= (!playback || playback->capacity() == frames * 4) && (!capture || capture->capacity() == frames * 4);
    if (capture) { ok &= capture->write(samples, 3) == 0; capture->clearBuffer(); }
    feeding = true; received = bad = 0;
    Serial.printf("STARTED %u\r\n", unsigned(ok));
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
