#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbAudioFunction audio(device); // UAC1. Pass EspUsbAudioProtocol::Uac2 for UAC2.
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();
uint8_t pcm[192];
uint32_t echoed = 0, lastReport = 0;

void setup() {
  Serial.begin(115200); // Separate debug connection.
  // Both directions share one clock, so they register the same rates.
  if (!playback.addFormat({48000, 1, 2, 16}) || !capture.addFormat({48000, 1, 2, 16})) {
    Serial.printf("Audio format failed: %s\n", device.lastErrorName());
    return;
  }
  EspUsbAudioConfig config;
  config.eventCapacity = 8;
  config.playbackBufferFrames = 480; // 10 ms each way: 480 * 2 bytes = 960 bytes.
  config.captureBufferFrames = 480;
  const esp_err_t configured = audio.configure(config);
  if (configured != ESP_OK) {
    Serial.printf("Audio configuration failed: %d\n", int(configured));
    return;
  }
  EspUsbDeviceConfig identity;
  identity.pid = 0x4024;
  identity.manufacturer = "EspUsb";
  identity.product = "EspUsbDevice Headset";
  identity.serialNumber = "espusb-headset";
  if (!device.begin(identity)) {
    Serial.printf("USB start failed: %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Headset ready: UAC1, 48000 Hz, 16-bit, mono");
}

void loop() {
  // Read only as much playback PCM as capture can take, so every byte read is
  // echoed. Playback that is not read in time is dropped by the playback ring
  // (DropOldest), not by a partial capture write.
  size_t room = capture.availableForWrite();
  if (room > sizeof(pcm)) room = sizeof(pcm);
  const size_t received = playback.read(pcm, room);
  if (received) echoed += capture.write(pcm, received);

  EspUsbAudioEvent event;
  while (audio.pollEvent(event)) {
    if (event.type == EspUsbAudioEventType::StreamStateChanged)
      Serial.printf("%s enabled=%u alt=%u\n",
          event.target == EspUsbAudioEventTarget::Playback ? "playback" : "capture",
          unsigned(event.enabled), unsigned(event.alternateSetting));
  }
  const uint32_t now = millis();
  if (uint32_t(now - lastReport) >= 1000) {
    const EspUsbAudioStreamStats in = playback.stats(), out = capture.stats();
    Serial.printf("echoed=%lu playback overruns=%lu capture underruns=%lu\n",
        static_cast<unsigned long>(echoed), static_cast<unsigned long>(in.overrunCount),
        static_cast<unsigned long>(out.underrunCount));
    echoed = 0;
    lastReport = now;
  }
  delay(1);
}
