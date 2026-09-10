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

// Block until the host has configured us, so every command below answers from a
// usable device whatever order the tests run in.
//
// device.ready() is tud_mounted(): the host completed SET_CONFIGURATION. That is
// the event the tests actually depend on, and it is answered on demand rather
// than announced once at boot - a test that does not run first never sees an
// announcement, which is what made most of this suite fail when its modules were
// run in reverse. tests/peer/usb_msc has had this shape all along and was the
// only peer module that survived that check.
static bool waitForHost(uint32_t timeoutMs = 5000)
{
  const uint32_t startedAt = millis();
  while (!device.ready() && millis() - startedAt < timeoutMs)
  {
    device.task();
    delay(10);
  }
  return device.ready();
}

void loop()
{
  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    const bool hostReady = waitForHost();
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
      Serial.printf("DEVICE_READY %u\n", hostReady ? 1 : 0);
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
