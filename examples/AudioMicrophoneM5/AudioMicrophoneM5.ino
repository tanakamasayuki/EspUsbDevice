// en: Send M5 microphone PCM as a USB capture stream.
// ja: M5のマイクPCMをUSB録音streamへ送る。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include <EspUsbDevice.h>
#include <M5Unified.h>


static constexpr uint32_t kSampleRate = 16000;
static constexpr size_t kBlockSamples = 240;  // 15 ms at 16 kHz mono
static constexpr size_t kRingBlocks = 3;       // >= 3 so the oldest block is complete

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();

static int16_t micRing[kRingBlocks][kBlockSamples];
static size_t recIdx = 0;
static bool micEnabled = false;
static uint32_t micTxBytes = 0;
static uint32_t lastLogMs = 0;

static int rateLineY = 0;
static uint32_t shownRate = 0xFFFFFFFFu;

// en: Draw the current USB Audio sample rate on a fixed screen line, repainting only when it changes. The fixed-width field overwrites any previous digits.
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
  M5.Display.println("AudioMicrophone M5");

  M5.Mic.setSampleRate(kSampleRate);
  if (!M5.Mic.begin())
  {
    Serial.println("MIC_BEGIN_FAILED");
    M5.Display.println("Mic failed");
    return;
  }

  // en: Declare rate, channels, bytes per sample and valid bits before the device starts.
  // ja: sample rate・channel・sampleあたりbyte数・有効bitを、device開始前に宣言する。
  capture.addFormat({kSampleRate, 1, 2, 16});

  EspUsbDeviceConfig usb;
  usb.vid = 0x303a;
  usb.pid = 0x4022;
  usb.manufacturer = "EspUsb";
  usb.product = "EspUsbDevice M5 Microphone";
  usb.serialNumber = "espusb-m5-microphone";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(usb))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    M5.Display.println("USB failed");
    return;
  }

  Serial.println("AudioMicrophoneM5 ready");
  M5.Display.println("USB mic ready");
  rateLineY = M5.Display.getCursorY();
  showSampleRate(audio.currentSampleRate());
}

void loop()
{
  M5.update();
  showSampleRate(audio.currentSampleRate());

  EspUsbAudioEvent event;
  // en: Drain recorded audio events here; stream state and USB enumeration are separate checks.
  // ja: 記録された音声eventをここで取り出す。stream状態とUSB列挙は別に確認する。
  while (audio.pollEvent(event))
  {
    if (event.type == EspUsbAudioEventType::StreamStateChanged &&
        event.target == EspUsbAudioEventTarget::Capture)
    {
      micEnabled = event.enabled;
      Serial.printf("AUDIO_INTERFACE mic %u\n",
                    event.enabled ? 1 : 0);
    }
  }

  // en: Enqueue the next capture block. When record() accepts it, the oldest block in the ring (kRingBlocks-1 iterations back) is complete; push that to the host. capture.write() feeds the bounded USB IN FIFO.
  // ja: 次の録音を受け付けたら、ring内の完成済みblockを有限のUSB IN bufferへ渡す。
  if (M5.Mic.isEnabled() && M5.Mic.record(micRing[recIdx], kBlockSamples, kSampleRate))
  {
    const size_t doneIdx = (recIdx + 1) % kRingBlocks;
    if (micEnabled)
    {
      // en: Capture sends PCM to the host; a finite buffer may accept fewer bytes than requested.
      // ja: CaptureはホストへPCMを送る。有限bufferのため要求した全byteを受け付けるとは限らない。
      const size_t written =
          capture.write(micRing[doneIdx],
                        kBlockSamples * sizeof(int16_t));
      if (written > 0)
      {
        micTxBytes += written;
      }
    }
    recIdx = (recIdx + 1) % kRingBlocks;
  }

  const uint32_t now = millis();
  if (now - lastLogMs >= 1000)
  {
    lastLogMs = now;
    const uint32_t tx = micTxBytes;
    micTxBytes = 0;
    Serial.printf("AUDIO_M5_MIC enabled=%u tx_bytes=%lu\n", micEnabled ? 1 : 0, static_cast<unsigned long>(tx));
  }
  delay(1);
}
