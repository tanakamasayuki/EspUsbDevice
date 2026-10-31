#include <EspUsbDevice.h>
#include <math.h>

EspUsbDevice device;
EspUsbAudioFunction audio(device); // UAC1. Pass EspUsbAudioProtocol::Uac2 for UAC2.
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();

constexpr uint32_t sampleRate = 48000;
constexpr float toneHz = 440.0f, twoPi = 2.0f * float(M_PI);
float phase = 0.0f;
int16_t block[48]; // 1 ms of mono PCM.
uint32_t written = 0, lastReport = 0;

void fillBlock() {
  for (int16_t &sample : block) {
    sample = int16_t(sinf(phase) * 8000.0f);
    phase += twoPi * toneHz / sampleRate;
    if (phase >= twoPi) phase -= twoPi;
  }
}

void setup() {
  Serial.begin(115200); // Separate debug connection.
  // {sampleRate, channels, bytesPerSample, bitsPerSample}
  if (!capture.addFormat({sampleRate, 1, 2, 16})) {
    Serial.printf("Audio format failed: %s\n", device.lastErrorName());
    return;
  }
  EspUsbAudioConfig config;
  config.eventCapacity = 4;
  config.captureBufferFrames = 480; // 10 ms: 480 * 2 bytes = 960 bytes.
  const esp_err_t configured = audio.configure(config);
  if (configured != ESP_OK) {
    Serial.printf("Audio configuration failed: %d\n", int(configured));
    return;
  }
  EspUsbDeviceConfig identity;
  identity.pid = 0x4022;
  identity.manufacturer = "EspUsb";
  identity.product = "EspUsbDevice Microphone";
  identity.serialNumber = "espusb-microphone";
  if (!device.begin(identity)) {
    Serial.printf("USB start failed: %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Microphone ready: UAC1, 48000 Hz, 16-bit, mono");
}

void loop() {
  // write() takes only what fits and never overwrites queued PCM, so generate
  // the next block only when it fits whole. Nothing generated is then lost.
  while (capture.availableForWrite() >= sizeof(block)) {
    fillBlock();
    written += capture.write(block, sizeof(block));
  }
  EspUsbAudioEvent event;
  while (audio.pollEvent(event)) {
    if (event.type == EspUsbAudioEventType::StreamStateChanged)
      Serial.printf("capture enabled=%u alt=%u\n", unsigned(event.enabled), unsigned(event.alternateSetting));
  }
  const uint32_t now = millis();
  if (uint32_t(now - lastReport) >= 1000) {
    // Underruns are silence the host received because the ring ran empty.
    const EspUsbAudioStreamStats stats = capture.stats();
    Serial.printf("written=%lu usb=%lu underruns=%lu/%lu bytes\n",
        static_cast<unsigned long>(written), static_cast<unsigned long>(stats.transferredBytes),
        static_cast<unsigned long>(stats.underrunCount), static_cast<unsigned long>(stats.underrunBytes));
    written = 0;
    lastReport = now;
  }
  delay(1);
}
