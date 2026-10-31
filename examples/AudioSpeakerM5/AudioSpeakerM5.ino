#include <EspUsbDevice.h>
#include <M5Unified.h>
#include <PCMFlowDeviceM5.h>

constexpr uint32_t sampleRate = 48000;
constexpr size_t maxPlayFrames = (sampleRate * 80u) / 1000u; // 80 ms player queue.

EspUsbDevice device;
EspUsbAudioFunction audio(device); // UAC1. Pass EspUsbAudioProtocol::Uac2 for UAC2.
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
M5SpeakerBufferedPlayer<maxPlayFrames> player;

uint8_t pcm[384];
uint32_t chunks = 0, usbBytes = 0, speakerBytes = 0, unsupported = 0, lastReport = 0;
int rateLineY = 0;
uint32_t shownRate = 0xffffffffu;

// Redraw the rate line only when the host changes it; the fixed-width field
// overwrites the previous digits.
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
  M5.Display.println("AudioSpeaker M5");

  M5.Speaker.begin();
  M5.Speaker.setVolume(128);
  if (!player.begin({sampleRate, 1, 16}, M5SpeakerBufferedPlayer<maxPlayFrames>::stableProfile())) {
    Serial.println("Player start failed");
    M5.Display.println("Player failed");
    return;
  }

  // {sampleRate, channels, bytesPerSample, bitsPerSample}
  if (!playback.addFormat({sampleRate, 2, 2, 16})) {
    Serial.printf("Audio format failed: %s\n", device.lastErrorName());
    return;
  }
  EspUsbAudioConfig config;
  config.eventCapacity = 8;
  config.playbackBufferFrames = 480; // 10 ms: 480 * 4 bytes = 1920 bytes.
  const esp_err_t configured = audio.configure(config);
  if (configured != ESP_OK) {
    Serial.printf("Audio configuration failed: %d\n", int(configured));
    return;
  }
  EspUsbDeviceConfig identity;
  identity.pid = 0x4021;
  identity.manufacturer = "EspUsb";
  identity.product = "EspUsbDevice M5 Speaker";
  identity.serialNumber = "espusb-m5-speaker";
  if (!device.begin(identity)) {
    Serial.printf("USB start failed: %s\n", device.lastErrorName());
    M5.Display.println("USB failed");
    return;
  }
  Serial.println("AudioSpeakerM5 ready");
  M5.Display.println("USB audio ready");
  rateLineY = M5.Display.getCursorY();
  showSampleRate(audio.currentSampleRate());
}

void loop() {
  M5.update();
  showSampleRate(audio.currentSampleRate());

  const size_t received = playback.read(pcm, sizeof(pcm));
  if (received) {
    ++chunks;
    usbBytes += received;
    // Pass the USB format with the PCM; 0 means the player did not accept it.
    const size_t queued = player.writePcm(pcm, received, PCMFormat{sampleRate, 2, 16});
    if (queued) speakerBytes += queued;
    else ++unsupported;
  }

  EspUsbAudioEvent event;
  while (audio.pollEvent(event)) {
    if (event.type == EspUsbAudioEventType::StreamStateChanged &&
        event.target == EspUsbAudioEventTarget::Playback) {
      Serial.printf("speaker %u\n", unsigned(event.enabled));
      if (!event.enabled) player.stop();
    }
  }

  const uint32_t now = millis();
  if (uint32_t(now - lastReport) >= 1000) {
    const EspUsbAudioStreamStats stats = playback.stats();
    Serial.printf("chunks=%lu bytes=%lu speaker=%lu unsupported=%lu usb overruns=%lu "
        "play_chunks=%lu waits=%lu gaps=%lu drops=%lu\n",
        static_cast<unsigned long>(chunks), static_cast<unsigned long>(usbBytes),
        static_cast<unsigned long>(speakerBytes), static_cast<unsigned long>(unsupported),
        static_cast<unsigned long>(stats.overrunCount),
        static_cast<unsigned long>(player.chunks()), static_cast<unsigned long>(player.waits()),
        static_cast<unsigned long>(player.gapRisks()), static_cast<unsigned long>(player.overflowDrops()));
    chunks = usbBytes = speakerBytes = unsupported = 0;
    lastReport = now;
  }
  delay(1);
}
