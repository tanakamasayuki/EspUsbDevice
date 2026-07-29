#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();

static uint32_t receivedAudioBytes = 0;
static bool receivedAudioReported = false;
static uint32_t volumeEventCount = 0;
static uint32_t muteEventCount = 0;
void setup()
{
  Serial.begin(115200);
  delay(5000);

  playback.addFormat({48000, 1, 2, 16});

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
      playback.resetStats();
      audio.clearEvents();
      Serial.println("DEVICE_AUDIO_RESET");
    }
    else if (command == '?')
    {
      // Liveness probe. If the device rebooted during a volume flood, setup()
      // reruns and prints AUDIO_DEVICE_READY again; a healthy device answers
      // here with its accumulated event counts and never reset.
      const EspUsbAudioStreamStats stats = playback.stats();
      Serial.printf(
          "DEVICE_ALIVE rx=%lu usb=%lu overruns=%lu vol=%lu mute=%lu events=%lu\n",
          static_cast<unsigned long>(receivedAudioBytes),
          static_cast<unsigned long>(stats.transferredBytes),
          static_cast<unsigned long>(stats.overrunCount),
          static_cast<unsigned long>(volumeEventCount),
          static_cast<unsigned long>(muteEventCount),
          static_cast<unsigned long>(audio.droppedEvents()));
    }
  }

  EspUsbAudioEvent event;
  while (audio.pollEvent(event))
  {
    if (event.type == EspUsbAudioEventType::StreamStateChanged)
    {
      Serial.printf("AUDIO_INTERFACE PLAYBACK %u alt=%u\n",
                    event.enabled ? 1 : 0, event.alternateSetting);
    }
    else if (event.type == EspUsbAudioEventType::VolumeChanged)
    {
      volumeEventCount++;
      Serial.printf("DEV_VOL ch=%u db=%d n=%lu\n",
                    event.channel, event.volumeDb256,
                    static_cast<unsigned long>(volumeEventCount));
    }
    else if (event.type == EspUsbAudioEventType::MuteChanged)
    {
      muteEventCount++;
      Serial.printf("DEV_MUTE ch=%u m=%u n=%lu\n",
                    event.channel, event.muted ? 1 : 0,
                    static_cast<unsigned long>(muteEventCount));
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
