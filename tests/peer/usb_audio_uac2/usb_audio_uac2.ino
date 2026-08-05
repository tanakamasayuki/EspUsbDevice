#include "EspUsbHost.h"

// DUT for the UAC2 peer test: an EspUsbHost host driving the EspUsbDevice UAC2
// headset in peer_device/. Each command prints one line for the pytest side.
//
// UAC2 facts the host can only learn by talking to the device (the sample rates
// live in the Clock Source entity, not in the descriptors) are printed on demand
// rather than from the connect callback.

EspUsbHost usb;

static uint32_t audioRxBytes = 0;
static int32_t audioRxMaxAbs = 0;
static uint8_t audioAddress = 0;
static int16_t outputSamples[480];

void setup()
{
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          audioAddress = device.address;
                          Serial.printf("DEVICE_CONNECTED addr=%u vid=%04x pid=%04x\n",
                                        device.address, device.vid, device.pid);
                          if (usb.audioOutputReady(device.address))
                          {
                            Serial.printf("AUDIO_OUT_READY addr=%u\n", device.address);
                          }
                          if (usb.audioInputReady(device.address))
                          {
                            Serial.printf("AUDIO_IN_READY addr=%u\n", device.address);
                          } });

  usb.onAudioData([](const EspUsbHostAudioData &audio)
                  {
                    audioRxBytes += audio.length;
                    const int16_t *samples = reinterpret_cast<const int16_t *>(audio.data);
                    for (size_t i = 0; i + 1 < audio.length; i += 2)
                    {
                      const int32_t value = samples[i / 2] < 0 ? -samples[i / 2] : samples[i / 2];
                      if (value > audioRxMaxAbs)
                      {
                        audioRxMaxAbs = value;
                      }
                    } });

  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

static void fillOutputSamples()
{
  static int16_t value = 0;
  for (size_t i = 0; i < sizeof(outputSamples) / sizeof(outputSamples[0]); i++)
  {
    outputSamples[i] = value;
    value = static_cast<int16_t>(value + 257);
  }
}

static void printAudioStreams()
{
  EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
  const size_t streamCount = usb.getAudioStreams(audioAddress, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
  for (size_t i = 0; i < streamCount; i++)
  {
    Serial.printf("AUDIO_STREAM iface=%u alt=%u ep=0x%02x dir=%s channels=%u bytes=%u bits=%u rate=%lu rates=%u min=%lu max=%lu proto=0x%02x terminal=%u clock=%u startable=%u\n",
                  streams[i].interfaceNumber,
                  streams[i].alternate,
                  streams[i].endpointAddress,
                  streams[i].input ? "IN" : "OUT",
                  streams[i].channels,
                  streams[i].bytesPerSample,
                  streams[i].bitsPerSample,
                  static_cast<unsigned long>(streams[i].sampleRate),
                  streams[i].sampleRateCount,
                  static_cast<unsigned long>(streams[i].sampleRateMin),
                  static_cast<unsigned long>(streams[i].sampleRateMax),
                  streams[i].protocol,
                  streams[i].terminalLink,
                  streams[i].clockSourceId,
                  streams[i].startable ? 1 : 0);
  }
  Serial.printf("AUDIO_STREAM_COUNT %u\n", static_cast<unsigned>(streamCount));
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    if (command == 'i')
    {
      // The device can re-enumerate a few times at startup, and its UAC2 rates
      // only arrive once the asynchronous clock query completes, so poll for a
      // stable ready address instead of reporting the connect callback's view.
      const uint32_t start = millis();
      while (!(audioAddress != 0 && usb.audioOutputReady(audioAddress) &&
               usb.audioInputReady(audioAddress)) &&
             millis() - start < 15000)
      {
        delay(50);
      }
      Serial.printf("HOST_AUDIO addr=%u out=%u in=%u\n",
                    audioAddress,
                    usb.audioOutputReady(audioAddress) ? 1 : 0,
                    usb.audioInputReady(audioAddress) ? 1 : 0);
    }
    else if (command == 'd')
    {
      printAudioStreams();
    }
    else if (command == 'u')
    {
      EspUsbHostAudioFeatureUnitInfo units[ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS];
      const size_t unitCount = usb.getAudioFeatureUnits(audioAddress, units, ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS);
      for (size_t i = 0; i < unitCount; i++)
      {
        Serial.printf("AUDIO_UNIT unit=%u source=%u channels=%u control_size=%u master=0x%lx proto=0x%02x mute=%u volume=%u\n",
                      units[i].unitId,
                      units[i].sourceId,
                      units[i].channelCount,
                      units[i].controlSize,
                      static_cast<unsigned long>(units[i].masterControls),
                      units[i].protocol,
                      usb.audioHasMute(audioAddress, units[i].unitId) ? 1 : 0,
                      usb.audioHasVolume(audioAddress, units[i].unitId) ? 1 : 0);
      }
      Serial.printf("AUDIO_UNIT_COUNT %u\n", static_cast<unsigned>(unitCount));
    }
    else if (command == 'v')
    {
      EspUsbHostAudioVolumeRange range;
      const bool ok = usb.audioGetVolumeRange(range, audioAddress);
      Serial.printf("AUDIO_VOLUME_RANGE ok=%u min=%d max=%d res=%d\n",
                    ok ? 1 : 0, range.min, range.max, range.resolution);
    }
    else if (command == 'w')
    {
      // Master channel volume, written and read back over the UAC2 CUR request.
      const bool set = usb.audioSetVolume(-6 * 256, audioAddress);
      int16_t volume = 0;
      const bool got = usb.audioGetVolume(volume, audioAddress);
      Serial.printf("AUDIO_VOLUME set=%u get=%u volume=%d\n",
                    set ? 1 : 0, got ? 1 : 0, volume);
    }
    else if (command == 'M')
    {
      const bool set = usb.audioSetMute(true, audioAddress);
      bool muted = false;
      const bool got = usb.audioGetMute(muted, audioAddress);
      Serial.printf("AUDIO_MUTE set=%u get=%u muted=%u\n",
                    set ? 1 : 0, got ? 1 : 0, muted ? 1 : 0);
    }
    else if (command == 'U')
    {
      const bool cleared = usb.audioSetMute(false, audioAddress);
      bool muted = true;
      const bool got = usb.audioGetMute(muted, audioAddress);
      Serial.printf("AUDIO_UNMUTE clear=%u get=%u muted=%u\n",
                    cleared ? 1 : 0, got ? 1 : 0, muted ? 1 : 0);
    }
    else if (command == 'c')
    {
      // Logical channel 1. Under UAC2 its controls come from the Feature Unit's
      // 4-byte bmaControls entry for that channel, not the UAC1 bit-per-control
      // layout, so this exercises a different decode than the master above.
      const bool capabilities =
          usb.audioHasMute(audioAddress, 0, 1) &&
          usb.audioHasVolume(audioAddress, 0, 1);
      const bool setOk =
          usb.audioSetMute(true, audioAddress, 0, 1) &&
          usb.audioSetVolume(-12 * 256, audioAddress, 0, 1);
      bool muted = false;
      int16_t volume = 0;
      EspUsbHostAudioVolumeRange range;
      const bool getOk =
          usb.audioGetMute(muted, audioAddress, 0, 1) &&
          usb.audioGetVolume(volume, audioAddress, 0, 1) &&
          usb.audioGetVolumeRange(range, audioAddress, 0, 1);
      Serial.printf(
          "CHANNEL_CONTROL caps=%u set=%u get=%u mute=%u volume=%d range=%d:%d:%d\n",
          capabilities ? 1 : 0, setOk ? 1 : 0, getOk ? 1 : 0,
          muted ? 1 : 0, volume, range.min, range.max, range.resolution);
    }
    else if (command == 'R')
    {
      // Program the Clock Source entity. Under UAC1 this would be an endpoint
      // request; under UAC2 it goes to the clock entity behind bTerminalLink.
      Serial.printf("AUDIO_RATE_SET %u\n",
                    usb.setAudioSampleRate(48000, audioAddress) ? 1 : 0);
    }
    else if (command == 'a')
    {
      Serial.printf("AUDIO_OUT_START %u\n",
                    usb.audioOutputStart(1, 16, 48000, audioAddress) ? 1 : 0);
    }
    else if (command == 'I')
    {
      Serial.printf("AUDIO_IN_START %u\n",
                    usb.audioInputStart(1, 16, 48000, audioAddress) ? 1 : 0);
    }
    else if (command == 's')
    {
      uint32_t sent = 0;
      fillOutputSamples();
      if (usb.audioSend(reinterpret_cast<const uint8_t *>(outputSamples), sizeof(outputSamples), audioAddress))
      {
        sent = sizeof(outputSamples);
      }
      Serial.printf("AUDIO_TX %lu\n", static_cast<unsigned long>(sent));
    }
    else if (command == 'f')
    {
      // The peer's playback interface is asynchronous, so it also declares an
      // explicit feedback IN endpoint. It must pace the host and must not have
      // been counted as a capture stream (see AUDIO_STREAM_COUNT).
      Serial.printf("AUDIO_FEEDBACK has=%u rate=%lu updates=%lu rejects=%lu pacing=%lu\n",
                    usb.audioOutputHasFeedback(audioAddress) ? 1 : 0,
                    static_cast<unsigned long>(usb.audioOutputFeedbackRate(audioAddress)),
                    static_cast<unsigned long>(usb.audioOutputFeedbackUpdates(audioAddress)),
                    static_cast<unsigned long>(usb.audioOutputFeedbackRejects(audioAddress)),
                    static_cast<unsigned long>(usb.audioOutputRate(audioAddress)));
    }
    else if (command == 'r')
    {
      audioRxBytes = 0;
      audioRxMaxAbs = 0;
      Serial.println("AUDIO_RESET");
    }
    else if (command == '?')
    {
      Serial.printf("HOST_RX bytes=%lu maxAbs=%ld\n",
                    static_cast<unsigned long>(audioRxBytes),
                    static_cast<long>(audioRxMaxAbs));
    }
  }
  delay(1);
}
