#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();

static uint32_t lastLogMs = 0;
static uint32_t receivedBytes = 0;

static void printEvents()
{
  EspUsbAudioEvent event;
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
  Serial.begin(115200);
  delay(1500);

  playback.addFormat({48000, 2, 2, 16});

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4020;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Speaker";
  config.serialNumber = "espusb-audio-sink";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Audio speaker ready: UAC2, 48000 Hz, 16-bit, stereo");
}

void loop()
{
  uint8_t pcm[384];
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
