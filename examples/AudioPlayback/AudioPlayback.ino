#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
uint8_t pcm[192];
uint32_t consumed = 0, lastReport = 0;

void setup() {
  Serial.begin(115200);
  // Each PCM shape supports the same rates of the shared clock. The host
  // selects the alternate and rate; no rebuild or build_opt.h is needed.
  for (uint32_t rate : {24000u, 16000u}) {
    if (!playback.addFormat({rate, 2, 2, 16}) ||
        !playback.addFormat({rate, 2, 3, 24})) {
      Serial.printf("Audio format failed: %d\n", int(device.lastError()));
      return;
    }
  }
  EspUsbAudioConfig config;
  config.eventCapacity = 0;
  config.playbackBufferFrames = 32; // 32 * largest frame (6 bytes) = 192 bytes.
  const esp_err_t configured = audio.configure(config);
  if (configured != ESP_OK) {
    Serial.printf("Audio configuration failed: %d\n", int(configured));
    return;
  }
  if (!device.begin()) Serial.printf("Audio start failed: %d\n", int(device.lastError()));
}

void loop() {
  // This example consumes PCM and reports its format. An application can send
  // read() data to its codec here; the USB library does not require an I2S/DAC.
  consumed += playback.read(pcm, sizeof(pcm));
  const uint32_t now = millis();
  if (uint32_t(now - lastReport) >= 1000) {
    EspUsbAudioFormat format;
    if (playback.currentFormat(format)) {
      Serial.printf("%lu Hz, %u channels, %u bits in %u bytes: %lu bytes read\n",
          static_cast<unsigned long>(format.sampleRate), unsigned(format.channels),
          unsigned(format.bitsPerSample), unsigned(format.bytesPerSample),
          static_cast<unsigned long>(consumed));
    }
    consumed = 0;
    lastReport = now;
  }
  delay(1);
}
