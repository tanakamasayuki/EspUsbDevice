#include "EspUsbHost.h"
#include <stdlib.h>

// Host side of the USB Audio source (microphone) peer test: start the device's
// input stream and confirm device -> host PCM arrives and is non-silent.

EspUsbHost usb;

static volatile uint8_t audioAddress = 0;
static volatile uint32_t rxBytes = 0;
static volatile int32_t rxMaxAbs = 0;

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.printf("DEVICE_CONNECTED addr=%u class=0x%02x\n", device.address, device.deviceClass);

                          EspUsbHostAudioStreamInfo audioStreams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
                          const size_t audioStreamCount = usb.getAudioStreams(device.address, audioStreams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
                          if (audioStreamCount > 0)
                          {
                            audioAddress = device.address;
                          }
                          for (size_t i = 0; i < audioStreamCount; i++)
                          {
                            Serial.printf("AUDIO_STREAM iface=%u alt=%u ep=0x%02x dir=%s channels=%u bytes=%u bits=%u rate=%lu\n",
                                          audioStreams[i].interfaceNumber,
                                          audioStreams[i].alternate,
                                          audioStreams[i].endpointAddress,
                                          audioStreams[i].input ? "IN" : "OUT",
                                          audioStreams[i].channels,
                                          audioStreams[i].bytesPerSample,
                                          audioStreams[i].bitsPerSample,
                                          static_cast<unsigned long>(audioStreams[i].sampleRate));
                          }
                          if (usb.audioInputReady(device.address))
                          {
                            Serial.printf("AUDIO_IN_READY addr=%u\n", device.address);
                          } });

  usb.onAudioData([](const EspUsbHostAudioData &data)
                  {
                    rxBytes += data.length;
                    const int16_t *samples = reinterpret_cast<const int16_t *>(data.data);
                    for (size_t i = 0; i < data.length / 2; i++)
                    {
                      const int32_t magnitude = abs(static_cast<int32_t>(samples[i]));
                      if (magnitude > rxMaxAbs)
                      {
                        rxMaxAbs = magnitude;
                      }
                    } });

  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    if (command == 'i')
    {
      // Wait for a stable, audio-input-ready device before reporting.
      const uint32_t start = millis();
      while (!(audioAddress != 0 && usb.audioInputReady(audioAddress)) && millis() - start < 15000)
      {
        delay(50);
      }
      Serial.printf("HOST_AUDIO addr=%u ready=%u\n",
                    audioAddress,
                    usb.audioInputReady(audioAddress) ? 1 : 0);
    }
    else if (command == 'a')
    {
      Serial.printf("MIC_START %u\n", usb.audioInputStart(1, 16, 48000, audioAddress) ? 1 : 0);
    }
    else if (command == 'r')
    {
      rxBytes = 0;
      rxMaxAbs = 0;
      Serial.println("MIC_RESET");
    }
    else if (command == '?')
    {
      Serial.printf("HOST_RX bytes=%lu maxAbs=%ld\n",
                    static_cast<unsigned long>(rxBytes),
                    static_cast<long>(rxMaxAbs));
    }
  }
  delay(1);
}
