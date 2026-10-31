#include <EspUsbDevice.h>
#include <M5Unified.h>
#include <PCMFlowDeviceM5.h>

constexpr uint32_t sampleRate = 48000;
constexpr size_t maxPlayFrames = (sampleRate * 80u) / 1000u; // 80 ms player queue.
constexpr size_t blockSamples = 480; // 10 ms at 48 kHz mono.
constexpr size_t ringBlocks = 3;     // >= 3 so the oldest block is complete.

EspUsbDevice device;
EspUsbAudioFunction audio(device); // UAC1. Pass EspUsbAudioProtocol::Uac2 for UAC2.
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();
M5SpeakerBufferedPlayer<maxPlayFrames> player;

uint8_t pcm[384];
// M5.Mic.record() fills a block asynchronously, so the blocks stay global.
int16_t micRing[ringBlocks][blockSamples];
size_t recordIndex = 0;
bool micEnabled = false;
uint32_t speakerBytes = 0, sentBytes = 0, rejectedBytes = 0, lastReport = 0;
int rateLineY = 0;
uint32_t shownRate = 0xffffffffu;

// Speaker and microphone share this one rate.
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
  M5.Display.println("AudioHeadset M5");

  M5.Speaker.begin();
  M5.Speaker.setVolume(128);
  M5.Mic.setSampleRate(sampleRate);
  M5.Mic.begin();
  if (!player.begin({sampleRate, 1, 16}, M5SpeakerBufferedPlayer<maxPlayFrames>::stableProfile())) {
    Serial.println("Player start failed");
    M5.Display.println("Player failed");
    return;
  }

  // One clock: stereo speaker and mono microphone register the same rate.
  if (!playback.addFormat({sampleRate, 2, 2, 16}) || !capture.addFormat({sampleRate, 1, 2, 16})) {
    Serial.printf("Audio format failed: %s\n", device.lastErrorName());
    return;
  }
  EspUsbAudioConfig config;
  config.eventCapacity = 8;
  config.playbackBufferFrames = 480;            // 10 ms: 1920 bytes.
  config.captureBufferFrames = 2 * blockSamples; // Two mic blocks: 1920 bytes.
  const esp_err_t configured = audio.configure(config);
  if (configured != ESP_OK) {
    Serial.printf("Audio configuration failed: %d\n", int(configured));
    return;
  }
  EspUsbDeviceConfig identity;
  identity.pid = 0x4024;
  identity.manufacturer = "EspUsb";
  identity.product = "EspUsbDevice M5 Headset";
  identity.serialNumber = "espusb-m5-headset";
  if (!device.begin(identity)) {
    Serial.printf("USB start failed: %s\n", device.lastErrorName());
    M5.Display.println("USB failed");
    return;
  }
  Serial.println("AudioHeadsetM5 ready");
  M5.Display.println("USB headset ready");
  rateLineY = M5.Display.getCursorY();
  showSampleRate(audio.currentSampleRate());
}

void loop() {
  M5.update();
  showSampleRate(audio.currentSampleRate());

  // Host -> device: queue received PCM on the M5 speaker.
  const size_t received = playback.read(pcm, sizeof(pcm));
  if (received) speakerBytes += player.writePcm(pcm, received, PCMFormat{sampleRate, 2, 16});

  EspUsbAudioEvent event;
  while (audio.pollEvent(event)) {
    if (event.type != EspUsbAudioEventType::StreamStateChanged) continue;
    if (event.target == EspUsbAudioEventTarget::Playback) {
      if (!event.enabled) player.stop();
      Serial.printf("speaker %u\n", unsigned(event.enabled));
    } else if (event.target == EspUsbAudioEventTarget::Capture) {
      micEnabled = event.enabled;
      Serial.printf("mic %u\n", unsigned(event.enabled));
    }
  }

  // Device -> host: send the oldest completed mic block, never the one being
  // recorded.
  if (micEnabled && M5.Mic.isEnabled() && M5.Mic.record(micRing[recordIndex], blockSamples, sampleRate)) {
    const size_t doneIndex = (recordIndex + 1) % ringBlocks;
    const size_t bytes = blockSamples * sizeof(int16_t);
    const size_t written = capture.write(micRing[doneIndex], bytes); // Takes only what fits.
    sentBytes += written;
    rejectedBytes += bytes - written;
    recordIndex = doneIndex;
  }

  const uint32_t now = millis();
  if (uint32_t(now - lastReport) >= 1000) {
    Serial.printf("speaker=%lu mic sent=%lu rejected=%lu\n", static_cast<unsigned long>(speakerBytes),
        static_cast<unsigned long>(sentBytes), static_cast<unsigned long>(rejectedBytes));
    speakerBytes = sentBytes = rejectedBytes = 0;
    lastReport = now;
  }
  delay(1);
}
