#include "EspUsbDevice.h"

// USB Audio headset (speaker + microphone on one device). Verifies both
// directions at once: it counts speaker PCM received from the host (host ->
// device) and continuously streams a loud sawtooth to the host (device ->
// host). The two paths are independent so each direction can be checked on its
// own, regardless of the other's timing.

EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();

static uint32_t rxBytes = 0;
static bool rxReported = false;
static uint32_t micTxBytes = 0;
static int16_t genValue = 0;
static bool captureEnabled = false;

void setup()
{
  Serial.begin(115200);
  delay(5000);

  playback.addFormat({48000, 1, 2, 16});
  capture.addFormat({48000, 1, 2, 16});

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4024;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Headset Peer";
  config.serialNumber = "espusb-headset-peer";

  const bool ok = device.begin(config);
  Serial.printf("HEADSET_DEVICE_READY %u error=%s\n", ok ? 1 : 0, device.lastErrorName());
}

void loop()
{
  uint8_t received[192];
  const size_t receivedLength =
      playback.read(received, sizeof(received));
  rxBytes += receivedLength;
  if (!rxReported && rxBytes >= 96)
  {
    rxReported = true;
    Serial.printf("DEVICE_RX_AUDIO %lu\n",
                  static_cast<unsigned long>(rxBytes));
  }

  EspUsbAudioEvent event;
  while (audio.pollEvent(event))
  {
    if (event.type == EspUsbAudioEventType::StreamStateChanged)
    {
      if (event.target == EspUsbAudioEventTarget::Capture)
      {
        captureEnabled = event.enabled;
      }
      const char *target =
          event.target == EspUsbAudioEventTarget::Playback ? "SPK" : "MIC";
      Serial.printf("AUDIO_INTERFACE %s %u alt=%u\n",
                    target, event.enabled ? 1 : 0,
                    event.alternateSetting);
    }
  }

  if (captureEnabled)
  {
    int16_t samples[48];
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
    {
      samples[i] = genValue;
      genValue = static_cast<int16_t>(genValue + 1024);
    }
    micTxBytes += capture.write(samples, sizeof(samples));
  }

  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    if (command == 'r')
    {
      rxBytes = 0;
      rxReported = false;
      playback.resetStats();
      capture.resetStats();
      Serial.println("HEADSET_RESET");
    }
    else if (command == '?')
    {
      const EspUsbAudioStreamStats playbackStats = playback.stats();
      const EspUsbAudioStreamStats captureStats = capture.stats();
      Serial.printf(
          "HEADSET_ALIVE rx=%lu tx=%lu usb_rx=%lu usb_tx=%lu "
          "play_overruns=%lu cap_underruns=%lu events=%lu\n",
                    static_cast<unsigned long>(rxBytes),
                    static_cast<unsigned long>(micTxBytes),
                    static_cast<unsigned long>(
                        playbackStats.transferredBytes),
                    static_cast<unsigned long>(
                        captureStats.transferredBytes),
                    static_cast<unsigned long>(
                        playbackStats.overrunCount),
                    static_cast<unsigned long>(
                        captureStats.underrunCount),
                    static_cast<unsigned long>(audio.droppedEvents()));
    }
  }
  delay(1);
}
