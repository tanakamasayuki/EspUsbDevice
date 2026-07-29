#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();

static uint32_t receivedAudioBytes = 0;
static bool receivedAudioReported = false;
void setup()
{
  Serial.begin(115200);
  delay(5000);

  playback.channels(1);
  playback.addFormat({48000, 2, 16});

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4021;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Audio Peer";
  config.serialNumber = "espusb-audio-peer";

  const bool ok = device.begin(config);
  Serial.printf("AUDIO_DEVICE_READY %u error=%s\n", ok ? 1 : 0, device.lastErrorName());
}

void loop()
{
  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    if (command == 'r')
    {
      receivedAudioBytes = 0;
      receivedAudioReported = false;
      Serial.println("DEVICE_AUDIO_RESET");
    }
    else if (command == '?')
    {
      // Liveness probe. If the device rebooted during a volume flood, setup()
      // reruns and prints AUDIO_DEVICE_READY again; a healthy device answers
      // here with its accumulated event counts and never reset.
      Serial.println("DEVICE_ALIVE");
    }
  }

  uint8_t pcm[256];
  const size_t received = playback.read(pcm, sizeof(pcm));
  receivedAudioBytes += received;
  if (!receivedAudioReported && receivedAudioBytes >= 96)
  {
    receivedAudioReported = true;
    Serial.printf("DEVICE_RX_AUDIO %lu\n",
                  static_cast<unsigned long>(receivedAudioBytes));
  }
  delay(1);
}
