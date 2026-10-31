#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbAudioFunction audio(device); // UAC1. Pass EspUsbAudioProtocol::Uac2 for UAC2.
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
uint8_t pcm[384];
uint32_t received = 0, lastReport = 0;

void printEvents() {
  // Events are queued by the USB worker and read here, on the sketch task.
  EspUsbAudioEvent event;
  while (audio.pollEvent(event)) {
    switch (event.type) {
    case EspUsbAudioEventType::StreamStateChanged:
      Serial.printf("stream enabled=%u alt=%u\n", unsigned(event.enabled), unsigned(event.alternateSetting));
      break;
    case EspUsbAudioEventType::SampleRateChanged:
      Serial.printf("rate %lu Hz\n", static_cast<unsigned long>(event.sampleRate));
      break;
    case EspUsbAudioEventType::MuteChanged:
      Serial.printf("mute channel=%u %u\n", unsigned(event.channel), unsigned(event.muted));
      break;
    case EspUsbAudioEventType::VolumeChanged:
      Serial.printf("volume channel=%u %d/256 dB\n", unsigned(event.channel), int(event.volumeDb256));
      break;
    }
  }
}

void setup() {
  Serial.begin(115200); // Separate debug connection.
  // {sampleRate, channels, bytesPerSample, bitsPerSample}
  if (!playback.addFormat({48000, 2, 2, 16})) {
    Serial.printf("Audio format failed: %s\n", device.lastErrorName());
    return;
  }
  EspUsbAudioConfig config;
  config.eventCapacity = 16;
  config.playbackBufferFrames = 480; // 10 ms: 480 * 4 bytes = 1920 bytes.
  const esp_err_t configured = audio.configure(config);
  if (configured != ESP_OK) {
    Serial.printf("Audio configuration failed: %d\n", int(configured));
    return;
  }
  EspUsbDeviceConfig identity;
  identity.pid = 0x4020;
  identity.manufacturer = "EspUsb";
  identity.product = "EspUsbDevice Speaker";
  identity.serialNumber = "espusb-audio-sink";
  if (!device.begin(identity)) {
    Serial.printf("USB start failed: %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Speaker ready: UAC1, 48000 Hz, 16-bit, stereo");
}

void loop() {
  // No DAC is attached: the PCM is only counted. Send read() data to I2S or a
  // codec here. Volume and mute are reported, not applied to the PCM.
  received += playback.read(pcm, sizeof(pcm));
  printEvents();
  const uint32_t now = millis();
  if (uint32_t(now - lastReport) >= 1000) {
    const EspUsbAudioStreamStats stats = playback.stats();
    bool muted = false;
    int16_t volume = 0;
    audio.getMute(muted);
    audio.getVolume(volume);
    Serial.printf("read=%lu usb=%lu overruns=%lu/%lu bytes mute=%u volume=%d/256 dB dropped events=%lu\n",
        static_cast<unsigned long>(received), static_cast<unsigned long>(stats.transferredBytes),
        static_cast<unsigned long>(stats.overrunCount), static_cast<unsigned long>(stats.overrunBytes),
        unsigned(muted), int(volume), static_cast<unsigned long>(audio.droppedEvents()));
    received = 0;
    lastReport = now;
  }
  delay(1);
}
