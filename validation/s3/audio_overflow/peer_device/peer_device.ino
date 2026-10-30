#include <Arduino.h>
#include <EspUsbDevice.h>
#include <algorithm>
EspUsbDevice device;
EspUsbAudioFunction *audio = nullptr;
EspUsbAudioPlaybackStream *playback = nullptr;
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
bool frameOk(const uint8_t *p, unsigned f, unsigned &k) {
  if (p[2] != 0xc5 || p[3] != 0x3a) return false;
  k = p[0] | unsigned(p[1]) << 8;
  for (unsigned j = 4; j < f; ++j) if (p[j] != uint8_t(0x40 + j + k)) return false;
  return true;
}
// Reads up to limit frames as runs of consecutive frame numbers, e.g. 2-4,48-49.
void report(const char *tag, size_t limit) {
  EspUsbAudioFormat format{}; bool valid = playback->currentFormat(format);
  const unsigned f = format.channels * format.bytesPerSample;
  valid &= f >= 4 && f <= 8;
  uint8_t data[64]; unsigned count = 0, last = 0; String runs;
  while (valid && count < limit) {
    const size_t n = playback->read(data, std::min(sizeof(data) / f, limit - count) * f);
    if (!n) break;
    if (n % f) { valid = false; break; }
    for (size_t i = 0; i < n; i += f) {
      unsigned k = 0;
      if (!frameOk(data + i, f, k)) valid = false;
      if (!count || k != last + 1) {
        if (count) runs += String(last) + ",";
        runs += String(k) + "-";
      }
      last = k; ++count;
    }
  }
  if (count) runs += String(last); else runs = "none";
  const auto s = playback->stats();
  Serial.printf("%s valid=%u frames=%u seq=%s left=%d transferred=%u overruns=%u dropped=%u\r\n",
                tag, unsigned(valid), count, runs.c_str(), playback->available(),
                unsigned(s.transferredBytes), unsigned(s.overrunCount), unsigned(s.overrunBytes));
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ")) {
    unsigned policy = 0, frames = 0; sscanf(command.c_str(), "start %u %u", &policy, &frames);
    delete audio; audio = new EspUsbAudioFunction(device, EspUsbAudioProtocol::Uac1);
    playback = &audio->addPlaybackStream();
    bool ok = playback->addFormat({24000, 2, 2, 16});
    // Same alternates as audio_alternates: 2/3/4-byte subslots, largest frame 8 bytes.
    for (unsigned r : {24000u, 16000u}) for (unsigned bytes : {2u, 3u, 4u})
      if (r != 24000 || bytes != 2) ok &= playback->addFormat({r, 2, uint8_t(bytes), uint8_t(bytes * 8)});
    for (unsigned r : {24000u, 16000u}) ok &= playback->addFormat({r, 2, 4, 24});
    EspUsbAudioConfig config; config.eventCapacity = 0; config.playbackBufferFrames = frames;
    ok &= config.playbackOverflowPolicy == EspUsbAudioOverflowPolicy::DropOldest;
    if (policy < 2) config.playbackOverflowPolicy = EspUsbAudioOverflowPolicy(policy);
    ok &= audio->configure(config) == ESP_OK;
    EspUsbAudioConfig invalid = config; invalid.playbackOverflowPolicy = EspUsbAudioOverflowPolicy(7);
    ok &= audio->configure(invalid) == ESP_ERR_INVALID_ARG;
    ok &= audio->configuration().playbackOverflowPolicy == config.playbackOverflowPolicy;
    ok &= device.begin();
    EspUsbAudioConfig other = config;
    other.playbackOverflowPolicy = policy == 0 ? EspUsbAudioOverflowPolicy::DropOldest : EspUsbAudioOverflowPolicy::DropNewest;
    ok &= audio->configure(other) == ESP_ERR_INVALID_STATE;
    ok &= audio->configuration().playbackOverflowPolicy == config.playbackOverflowPolicy;
    ok &= playback->capacity() == frames * 8;
    Serial.printf("STARTED %u policy=%u capacity=%u\r\n", unsigned(ok),
                  unsigned(audio->configuration().playbackOverflowPolicy), unsigned(playback->capacity()));
  } else if (command.startsWith("read ")) {
    report("READ", command.substring(5).toInt());
  } else if (command == "dump") {
    report("DUMP", SIZE_MAX);
  } else if (command == "empty") {
    const auto s = playback->stats();
    Serial.printf("EMPTY left=%d transferred=%u\r\n", playback->available(), unsigned(s.transferredBytes));
  } else if (command == "resetstats") {
    playback->resetStats(); Serial.println("STATS_RESET");
  } else if (command == "stop") { Serial.println(device.end() == ESP_OK ? "STOPPED" : "STOP_FAILED"); }
}
