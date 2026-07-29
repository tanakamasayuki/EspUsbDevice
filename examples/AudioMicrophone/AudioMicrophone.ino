#include "EspUsbDevice.h"
#include <math.h>

EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();

static constexpr float TONE_HZ = 440.0f;
static float phase = 0.0f;
static float phaseIncrement = 0.0f;

void setup()
{
  Serial.begin(115200);
  delay(1500);

  capture.addFormat({48000, 1, 2, 16});
  phaseIncrement =
      2.0f * static_cast<float>(M_PI) * TONE_HZ / 48000.0f;

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4022;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Microphone";
  config.serialNumber = "espusb-microphone";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Audio microphone ready: UAC2, 48000 Hz, 16-bit, mono");
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
  capture.write(samples, sizeof(samples));

  EspUsbAudioEvent event;
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
