#include "EspUsbHost.h"

EspUsbHost usb;

static uint32_t audioBytes = 0;
static bool audioReported = false;
static volatile uint8_t audioAddress = 0;
static int16_t outputSamples[480];

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.printf("DEVICE_CONNECTED addr=%u portId=0x%02x class=0x%02x\n",
                                        device.address,
                                        device.portId,
                                        device.deviceClass);
                          if (usb.audioOutputReady(device.address))
                          {
                            audioAddress = device.address;
                            Serial.printf("AUDIO_OUT_READY addr=%u\n", device.address);
                          }

                          EspUsbHostAudioStreamInfo audioStreams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
                          const size_t audioStreamCount = usb.getAudioStreams(device.address, audioStreams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
                          // Capture the audio device address for any audio device,
                          // even if audioOutputReady() is momentarily false at connect
                          // time, so serial-command tests do not depend on timing.
                          if (audioStreamCount > 0)
                          {
                            audioAddress = device.address;
                          }
                          for (size_t i = 0; i < audioStreamCount; i++)
                          {
                            Serial.printf("AUDIO_STREAM iface=%u alt=%u ep=0x%02x dir=%s channels=%u bytes=%u bits=%u rate=%lu rates=%u first=%lu min=%lu max=%lu maxPacket=%u interval=%u\n",
                                          audioStreams[i].interfaceNumber,
                                          audioStreams[i].alternate,
                                          audioStreams[i].endpointAddress,
                                          audioStreams[i].input ? "IN" : "OUT",
                                          audioStreams[i].channels,
                                          audioStreams[i].bytesPerSample,
                                          audioStreams[i].bitsPerSample,
                                          static_cast<unsigned long>(audioStreams[i].sampleRate),
                                          audioStreams[i].sampleRateCount,
                                          static_cast<unsigned long>(audioStreams[i].sampleRateCount > 0 ? audioStreams[i].sampleRates[0] : 0),
                                          static_cast<unsigned long>(audioStreams[i].sampleRateMin),
                                          static_cast<unsigned long>(audioStreams[i].sampleRateMax),
                                          audioStreams[i].maxPacketSize,
                                          audioStreams[i].interval);
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
    value += 257;
  }
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    if (command == 'r')
    {
      audioBytes = 0;
      audioReported = false;
      Serial.println("AUDIO_RESET");
    }
    else if (command == 'a')
    {
      Serial.printf("AUDIO_START %u\n", usb.audioOutputStart(1, 16, 48000, audioAddress) ? 1 : 0);
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
    else if (command == 'i')
    {
      // Wait for a stable, audio-output-ready device before reporting. The device
      // can re-enumerate a few times at startup (addr changes) and only becomes
      // audioOutputReady() once its audio streams are parsed, so poll for a ready
      // address rather than just any address. This lets a test synchronize
      // regardless of run order or boot timing.
      const uint32_t start = millis();
      while (!(audioAddress != 0 && usb.audioOutputReady(audioAddress)) && millis() - start < 15000)
      {
        delay(50);
      }
      Serial.printf("HOST_AUDIO addr=%u ready=%u\n",
                    audioAddress,
                    usb.audioOutputReady(audioAddress) ? 1 : 0);
    }
    else if (command == 'v')
    {
      // Reproduce a Windows volume-slider drag: many intermediate SET_CUR values
      // arriving back-to-back as fast as possible on the master channel.
      Serial.println("VOLUME_FLOOD_BEGIN");
      uint32_t count = 0;
      for (int sweep = 0; sweep < 6; sweep++)
      {
        for (int16_t v = -12800; v <= 0; v = static_cast<int16_t>(v + 128))
        {
          usb.audioSetVolume(v, audioAddress, 0, 0, 20);
          count++;
        }
        for (int16_t v = 0; v >= -12800; v = static_cast<int16_t>(v - 128))
        {
          usb.audioSetVolume(v, audioAddress, 0, 0, 20);
          count++;
        }
      }
      Serial.printf("VOLUME_FLOOD_DONE %lu\n", static_cast<unsigned long>(count));
    }
    else if (command == 'm')
    {
      // Rapid mute toggling on the master channel.
      Serial.println("MUTE_FLOOD_BEGIN");
      uint32_t count = 0;
      for (int i = 0; i < 300; i++)
      {
        usb.audioSetMute((i & 1) != 0, audioAddress, 0, 0, 20);
        count++;
      }
      Serial.printf("MUTE_FLOOD_DONE %lu\n", static_cast<unsigned long>(count));
    }
  }
  delay(1);
}
