// en: Connect USB playback/capture to M5 speaker and microphone hardware.
// ja: USBの再生・録音をM5のスピーカーとマイクへつなぐ。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include <EspUsbDevice.h>
#include <M5Unified.h>
#include <PCMFlowDeviceM5.h>


static constexpr uint32_t kSampleRate = 48000;
static constexpr size_t kMaxPlayFrames = (kSampleRate * 80u) / 1000u;
static constexpr size_t kBlockSamples = 480;  // 10 ms at 48 kHz mono
static constexpr size_t kRingBlocks = 3;       // >= 3 so the oldest block is complete

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();

M5SpeakerBufferedPlayer<kMaxPlayFrames> player;

static int16_t micRing[kRingBlocks][kBlockSamples];
static size_t recIdx = 0;
static bool micEnabled = false;
static bool speakerEnabled = false;

static int rateLineY = 0;
static uint32_t shownRate = 0xFFFFFFFFu;

// en: Draw the current USB Audio sample rate on a fixed screen line, repainting only when it changes. Speaker and microphone share this one rate. The fixed-width field overwrites any previous digits.
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
  M5.Display.println("AudioHeadset M5");

  M5.Speaker.begin();
  M5.Speaker.setVolume(128);
  M5.Mic.setSampleRate(kSampleRate);
  M5.Mic.begin();

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
  capture.addFormat({kSampleRate, 1, 2, 16});

  EspUsbDeviceConfig usb;
  usb.vid = 0x303a;
  usb.pid = 0x4024;
  usb.manufacturer = "EspUsb";
  usb.product = "EspUsbDevice M5 Headset";
  usb.serialNumber = "espusb-m5-headset";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(usb))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    M5.Display.println("USB failed");
    return;
  }

  Serial.println("AudioHeadsetM5 ready");
  M5.Display.println("USB headset ready");
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
    player.writePcm(pcm, received, {kSampleRate, 2, 16});
  }

  EspUsbAudioEvent event;
  // en: Drain recorded audio events here; stream state and USB enumeration are separate checks.
  // ja: 記録された音声eventをここで取り出す。stream状態とUSB列挙は別に確認する。
  while (audio.pollEvent(event))
  {
    if (event.type != EspUsbAudioEventType::StreamStateChanged)
    {
      continue;
    }
    if (event.target == EspUsbAudioEventTarget::Playback)
    {
      speakerEnabled = event.enabled;
      if (!event.enabled)
      {
        player.stop();
      }
      Serial.printf("AUDIO_INTERFACE speaker %u\n",
                    event.enabled ? 1 : 0);
    }
    else if (event.target == EspUsbAudioEventTarget::Capture)
    {
      micEnabled = event.enabled;
      Serial.printf("AUDIO_INTERFACE mic %u\n",
                    event.enabled ? 1 : 0);
    }
  }

  // en: Device -> host: enqueue a mic capture block and, when accepted, push the oldest (completed) block to the host.
  // ja: 録音中blockを再利用せず、ring内の完成済みblockをホストへ送る。
  if (micEnabled && M5.Mic.isEnabled() && M5.Mic.record(micRing[recIdx], kBlockSamples, kSampleRate))
  {
    const size_t doneIdx = (recIdx + 1) % kRingBlocks;
    // en: Capture sends PCM to the host; a finite buffer may accept fewer bytes than requested.
    // ja: CaptureはホストへPCMを送る。有限bufferのため要求した全byteを受け付けるとは限らない。
    capture.write(micRing[doneIdx],
                  kBlockSamples * sizeof(int16_t));
    recIdx = (recIdx + 1) % kRingBlocks;
  }
  delay(1);
}
