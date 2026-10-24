// en: Generate a synthetic tone as USB microphone input.
// ja: 合成した試験音をUSBマイク入力として送る。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"
#include <math.h>

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();

static constexpr float TONE_HZ = 440.0f;
static float phase = 0.0f;
static float phaseIncrement = 0.0f;

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  // en: Declare rate, channels, bytes per sample and valid bits before the device starts.
  // ja: sample rate・channel・sampleあたりbyte数・有効bitを、device開始前に宣言する。
  capture.addFormat({48000, 1, 2, 16});
  phaseIncrement =
      2.0f * static_cast<float>(M_PI) * TONE_HZ / 48000.0f;

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4022;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Microphone";
  config.serialNumber = "espusb-microphone";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Audio microphone ready: UAC1, 48000 Hz, 16-bit, mono");
}

void loop()
{
  int16_t samples[48];
  for (size_t i = 0; i < 48; ++i)
  {
    samples[i] = static_cast<int16_t>(sinf(phase) * 8000.0f);
    phase += phaseIncrement;
    if (phase >= 2.0f * static_cast<float>(M_PI))
    {
      phase -= 2.0f * static_cast<float>(M_PI);
    }
  }
  // en: Capture sends PCM to the host; a finite buffer may accept fewer bytes than requested.
  // ja: CaptureはホストへPCMを送る。有限bufferのため要求した全byteを受け付けるとは限らない。
  capture.write(samples, sizeof(samples));

  EspUsbAudioEvent event;
  // en: Drain recorded audio events here; stream state and USB enumeration are separate checks.
  // ja: 記録された音声eventをここで取り出す。stream状態とUSB列挙は別に確認する。
  while (audio.pollEvent(event))
  {
    if (event.type == EspUsbAudioEventType::StreamStateChanged)
    {
      Serial.printf("AUDIO_CAPTURE enabled=%u alt=%u\n",
                    event.enabled ? 1 : 0, event.alternateSetting);
    }
  }
  delay(1);
}
