#include <EspUsbDevice.h>
#include <M5Unified.h>
#include <PCMFlowDeviceM5.h>

static constexpr uint32_t kSampleRate = 48000;
static constexpr size_t kMaxPlayFrames = (kSampleRate * 80u) / 1000u;

EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();

M5SpeakerBufferedPlayer<kMaxPlayFrames> player;

static uint32_t audioChunks = 0;
static uint32_t audioBytes = 0;
static uint32_t speakerBytes = 0;
static uint32_t unsupportedChunks = 0;
static uint32_t lastLogMs = 0;

static int rateLineY = 0;
static uint32_t shownRate = 0xFFFFFFFFu;

// Draw the current USB Audio sample rate on a fixed screen line, repainting only
// when it changes (the host can change it at runtime via the sampling-freq
// control). The fixed-width field overwrites any previous digits.
static void showSampleRate(uint32_t rate)
{
  if (rate == shownRate)
  {
    return;
  }
  shownRate = rate;
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(0, rateLineY);
  M5.Display.printf("Rate:%6lu Hz", static_cast<unsigned long>(rate));
}

void setup()
{
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(1500);

  M5.Display.setTextSize(2);
  M5.Display.setCursor(0, 0);
  M5.Display.println("EspUsbDevice");
  M5.Display.println("AudioSpeaker M5");

  M5.Speaker.begin();
  M5.Speaker.setVolume(128);

  if (!player.begin({kSampleRate, 1, 16},
                    M5SpeakerBufferedPlayer<kMaxPlayFrames>::stableProfile()))
  {
    Serial.println("PLAYER_BEGIN_FAILED");
    M5.Display.println("Player failed");
    return;
  }

  playback.addFormat({kSampleRate, 2, 2, 16});

  EspUsbDeviceConfig usb;
  usb.vid = 0x303a;
  usb.pid = 0x4021;
  usb.manufacturer = "EspUsb";
  usb.product = "EspUsbDevice M5 Speaker";
  usb.serialNumber = "espusb-m5-speaker";

  if (!device.begin(usb))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    M5.Display.println("USB failed");
    return;
  }

  Serial.println("AudioSpeakerM5 ready");
  M5.Display.println("USB audio ready");
  rateLineY = M5.Display.getCursorY();
  showSampleRate(audio.currentSampleRate());
}

void loop()
{
  M5.update();
  showSampleRate(audio.currentSampleRate());

  uint8_t pcm[384];
  const size_t received = playback.read(pcm, sizeof(pcm));
  if (received)
  {
    audioChunks++;
    audioBytes += received;
    const PCMFormat format{kSampleRate, 2, 16};
    const size_t written = player.writePcm(pcm, received, format);
    if (written)
    {
      speakerBytes += written;
    }
    else
    {
      unsupportedChunks++;
    }
  }

  EspUsbAudioEvent event;
  while (audio.pollEvent(event))
  {
    if (event.type == EspUsbAudioEventType::StreamStateChanged &&
        event.target == EspUsbAudioEventTarget::Playback)
    {
      Serial.printf("AUDIO_INTERFACE speaker %u\n",
                    event.enabled ? 1 : 0);
      if (!event.enabled)
      {
        player.stop();
      }
    }
  }

  const uint32_t now = millis();
  if (now - lastLogMs >= 1000)
  {
    lastLogMs = now;
    const uint32_t chunks = audioChunks;
    const uint32_t bytes = audioBytes;
    const uint32_t speaker = speakerBytes;
    const uint32_t unsupported = unsupportedChunks;
    audioChunks = 0;
    audioBytes = 0;
    speakerBytes = 0;
    unsupportedChunks = 0;
    Serial.printf("AUDIO_M5 chunks=%lu bytes=%lu speaker_bytes=%lu unsupported=%lu play_chunks=%lu waits=%lu gaps=%lu drops=%lu\n",
                  static_cast<unsigned long>(chunks),
                  static_cast<unsigned long>(bytes),
                  static_cast<unsigned long>(speaker),
                  static_cast<unsigned long>(unsupported),
                  static_cast<unsigned long>(player.chunks()),
                  static_cast<unsigned long>(player.waits()),
                  static_cast<unsigned long>(player.gapRisks()),
                  static_cast<unsigned long>(player.overflowDrops()));
  }
  delay(1);
}
