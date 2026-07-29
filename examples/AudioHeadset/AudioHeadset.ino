#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();

void setup()
{
  Serial.begin(115200);
  delay(1500);

  playback.addFormat({48000, 1, 2, 16});
  capture.addFormat({48000, 1, 2, 16});

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4024;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Headset";
  config.serialNumber = "espusb-headset";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Audio headset ready: UAC2, 48000 Hz, 16-bit, mono");
}

void loop()
{
  uint8_t pcm[192];
  const size_t received = playback.read(pcm, sizeof(pcm));
  if (received)
  {
    capture.write(pcm, received);
  }

  EspUsbAudioEvent event;
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
