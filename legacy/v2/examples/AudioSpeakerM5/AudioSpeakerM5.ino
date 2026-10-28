// en: Play received USB PCM through an M5 speaker.
// ja: 受信したUSB PCMをM5のスピーカーで鳴らす。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include <EspUsbDevice.h>
#include <M5Unified.h>
#include <PCMFlowDeviceM5.h>

static constexpr uint32_t kSampleRate = 48000;
static constexpr size_t kMaxPlayFrames = (kSampleRate * 80u) / 1000u;

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
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

// en: Draw the current USB Audio sample rate on a fixed screen line, repainting only when it changes (the host can change it at runtime via the sampling-freq control). The fixed-width field overwrites any previous digits.
// ja: USB音声のsample rate表示は、値が変わった部分だけ描き直す。
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
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
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

  // en: Declare rate, channels, bytes per sample and valid bits before the device starts.
  // ja: sample rate・channel・sampleあたりbyte数・有効bitを、device開始前に宣言する。
  playback.addFormat({kSampleRate, 2, 2, 16});

  EspUsbDeviceConfig usb;
  usb.vid = 0x303a;
  usb.pid = 0x4021;
  usb.manufacturer = "EspUsb";
  usb.product = "EspUsbDevice M5 Speaker";
  usb.serialNumber = "espusb-m5-speaker";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
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
  // en: Read only bytes already buffered; application message boundaries must be tracked separately.
  // ja: bufferに届いたbyteを読む。アプリのメッセージの区切りは別に管理する。
  // en: Playback is host-to-device PCM; the return value is actual bytes, not audio frames.
  // ja: Playbackはホストから受けるPCM。戻り値は実byte数であり、frame数ではない。
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
  // en: Drain recorded audio events here; stream state and USB enumeration are separate checks.
  // ja: 記録された音声eventをここで取り出す。stream状態とUSB列挙は別に確認する。
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
