#include "EspUsbHost.h"

EspUsbHost usb;

static uint32_t audioBytes = 0;
static bool audioReported = false;
static uint8_t audioAddress = 0;
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
  }
  delay(1);
}
