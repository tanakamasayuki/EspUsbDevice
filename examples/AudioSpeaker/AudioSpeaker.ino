// en: Receive playback PCM without driving a physical speaker.
// ja: 物理スピーカーを駆動せず、再生PCMを受信する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();

static uint32_t lastLogMs = 0;
static uint32_t receivedBytes = 0;

static void printEvents()
{
  EspUsbAudioEvent event;
  // en: Drain recorded audio events here; stream state and USB enumeration are separate checks.
  // ja: 記録された音声eventをここで取り出す。stream状態とUSB列挙は別に確認する。
  while (audio.pollEvent(event))
  {
    if (event.type == EspUsbAudioEventType::StreamStateChanged)
    {
      Serial.printf("AUDIO_PLAYBACK enabled=%u alt=%u\n",
                    event.enabled ? 1 : 0, event.alternateSetting);
    }
    else if (event.type == EspUsbAudioEventType::MuteChanged)
    {
      Serial.printf("AUDIO_MUTE %u\n", event.muted ? 1 : 0);
    }
    else if (event.type == EspUsbAudioEventType::VolumeChanged)
    {
      Serial.printf("AUDIO_VOLUME_DB256 %d\n", event.volumeDb256);
    }
  }
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  // en: Declare rate, channels, bytes per sample and valid bits before the device starts.
  // ja: sample rate・channel・sampleあたりbyte数・有効bitを、device開始前に宣言する。
  playback.addFormat({48000, 2, 2, 16});

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4020;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Speaker";
  config.serialNumber = "espusb-audio-sink";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Audio speaker ready: UAC1, 48000 Hz, 16-bit, stereo");
}

void loop()
{
  uint8_t pcm[384];
  // en: Playback is host-to-device PCM; the return value is actual bytes, not audio frames.
  // ja: Playbackはホストから受けるPCM。戻り値は実byte数であり、frame数ではない。
  receivedBytes += playback.read(pcm, sizeof(pcm));
  printEvents();

  const uint32_t now = millis();
  if (now - lastLogMs >= 1000)
  {
    lastLogMs = now;
    const EspUsbAudioStreamStats stats = playback.stats();
    Serial.printf("AUDIO_RX bytes=%lu usb=%lu overruns=%lu dropped=%lu\n",
                  static_cast<unsigned long>(receivedBytes),
                  static_cast<unsigned long>(stats.transferredBytes),
                  static_cast<unsigned long>(stats.overrunCount),
                  static_cast<unsigned long>(audio.droppedEvents()));
    receivedBytes = 0;
  }
  delay(1);
}
