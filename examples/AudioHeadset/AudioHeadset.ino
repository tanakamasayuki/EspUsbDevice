// en: Loop received playback PCM back into a USB capture stream.
// ja: 受けた再生PCMをUSB録音streamへ折り返す。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  // en: Declare rate, channels, bytes per sample and valid bits before the device starts.
  // ja: sample rate・channel・sampleあたりbyte数・有効bitを、device開始前に宣言する。
  playback.addFormat({48000, 1, 2, 16});
  capture.addFormat({48000, 1, 2, 16});

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4024;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Headset";
  config.serialNumber = "espusb-headset";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Audio headset ready: UAC1, 48000 Hz, 16-bit, mono");
}

void loop()
{
  uint8_t pcm[192];
  // en: Read only bytes already buffered; application message boundaries must be tracked separately.
  // ja: bufferに届いたbyteを読む。アプリのメッセージの区切りは別に管理する。
  // en: Playback is host-to-device PCM; the return value is actual bytes, not audio frames.
  // ja: Playbackはホストから受けるPCM。戻り値は実byte数であり、frame数ではない。
  const size_t received = playback.read(pcm, sizeof(pcm));
  if (received)
  {
    // en: Capture sends PCM to the host; a finite buffer may accept fewer bytes than requested.
    // ja: CaptureはホストへPCMを送る。有限bufferのため要求した全byteを受け付けるとは限らない。
    capture.write(pcm, received);
  }

  EspUsbAudioEvent event;
  // en: Drain recorded audio events here; stream state and USB enumeration are separate checks.
  // ja: 記録された音声eventをここで取り出す。stream状態とUSB列挙は別に確認する。
  while (audio.pollEvent(event))
  {
    if (event.type == EspUsbAudioEventType::StreamStateChanged)
    {
      Serial.printf("AUDIO_STREAM target=%u enabled=%u alt=%u\n",
                    static_cast<unsigned>(event.target),
                    event.enabled ? 1 : 0, event.alternateSetting);
    }
  }
  delay(1);
}
