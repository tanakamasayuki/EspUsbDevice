#include <EspUsbDevice.h>
#include <M5Unified.h>

constexpr uint32_t sampleRate = 16000;
constexpr size_t blockSamples = 240; // 15 ms at 16 kHz mono.
constexpr size_t ringBlocks = 3;     // >= 3 so the oldest block is complete.

EspUsbDevice device;
EspUsbAudioFunction audio(device); // UAC1. Pass EspUsbAudioProtocol::Uac2 for UAC2.
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();

// M5.Mic.record() fills a block asynchronously, so the blocks stay global.
int16_t micRing[ringBlocks][blockSamples];
size_t recordIndex = 0;
bool micEnabled = false;
uint32_t sentBytes = 0, rejectedBytes = 0, lastReport = 0;
int rateLineY = 0;
uint32_t shownRate = 0xffffffffu;

void showSampleRate(uint32_t rate) {
  if (rate == shownRate) return;
  shownRate = rate;
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(0, rateLineY);
  M5.Display.printf("Rate:%6lu Hz", static_cast<unsigned long>(rate));
}

void setup() {
  M5.begin(M5.config());
  Serial.begin(115200); // Separate debug connection.
  M5.Display.setTextSize(2);
  M5.Display.setCursor(0, 0);
  M5.Display.println("EspUsbDevice");
  M5.Display.println("AudioMicrophone M5");

  M5.Mic.setSampleRate(sampleRate);
  if (!M5.Mic.begin()) {
    Serial.println("Mic start failed");
    M5.Display.println("Mic failed");
    return;
  }

  // {sampleRate, channels, bytesPerSample, bitsPerSample}
  if (!capture.addFormat({sampleRate, 1, 2, 16})) {
    Serial.printf("Audio format failed: %s\n", device.lastErrorName());
    return;
  }
  EspUsbAudioConfig config;
  config.eventCapacity = 4;
  config.captureBufferFrames = 2 * blockSamples; // Two mic blocks: 960 bytes.
  const esp_err_t configured = audio.configure(config);
  if (configured != ESP_OK) {
    Serial.printf("Audio configuration failed: %d\n", int(configured));
    return;
  }
  EspUsbDeviceConfig identity;
  identity.pid = 0x4022;
  identity.manufacturer = "EspUsb";
  identity.product = "EspUsbDevice M5 Microphone";
  identity.serialNumber = "espusb-m5-microphone";
  if (!device.begin(identity)) {
    Serial.printf("USB start failed: %s\n", device.lastErrorName());
    M5.Display.println("USB failed");
    return;
  }
  Serial.println("AudioMicrophoneM5 ready");
  M5.Display.println("USB mic ready");
  rateLineY = M5.Display.getCursorY();
  showSampleRate(audio.currentSampleRate());
}

void loop() {
  M5.update();
  showSampleRate(audio.currentSampleRate());

  EspUsbAudioEvent event;
  while (audio.pollEvent(event)) {
    if (event.type == EspUsbAudioEventType::StreamStateChanged &&
        event.target == EspUsbAudioEventTarget::Capture) {
      micEnabled = event.enabled;
      Serial.printf("mic %u\n", unsigned(event.enabled));
    }
  }

  // When record() accepts the next block, the oldest block in the ring is
  // complete; send that one, never the block being recorded.
  if (M5.Mic.isEnabled() && M5.Mic.record(micRing[recordIndex], blockSamples, sampleRate)) {
    const size_t doneIndex = (recordIndex + 1) % ringBlocks;
    if (micEnabled) {
      // write() takes only what fits; the rest of this block is dropped.
      const size_t bytes = blockSamples * sizeof(int16_t);
      const size_t written = capture.write(micRing[doneIndex], bytes);
      sentBytes += written;
      rejectedBytes += bytes - written;
    }
    recordIndex = doneIndex;
  }

  const uint32_t now = millis();
  if (uint32_t(now - lastReport) >= 1000) {
    const EspUsbAudioStreamStats stats = capture.stats();
    Serial.printf("mic enabled=%u sent=%lu rejected=%lu underruns=%lu\n", unsigned(micEnabled),
        static_cast<unsigned long>(sentBytes), static_cast<unsigned long>(rejectedBytes),
        static_cast<unsigned long>(stats.underrunCount));
    sentBytes = rejectedBytes = 0;
    lastReport = now;
  }
  delay(1);
}
