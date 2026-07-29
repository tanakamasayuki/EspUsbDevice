#include "EspUsbHost.h"

EspUsbHost usb;

static constexpr uint16_t DEVICE_PID = 0x4027;
static volatile uint8_t deviceAddress = 0;
static int16_t outputSamples[480];

static void reportEnumeration()
{
  const uint8_t address = deviceAddress;
  if (address == 0)
  {
    Serial.println(
        "HOST_ENUM pid=0000 ifcount=0 eps=0 dup=1 hid=0 audio=0 claimok=0");
    return;
  }

  EspUsbHostInterfaceInfo interfaces[8];
  const size_t interfaceCount =
      usb.getInterfaces(address, interfaces, 8);
  uint8_t hidCount = 0;
  uint8_t audioCount = 0;
  uint8_t claimOk = 1;
  for (size_t i = 0; i < interfaceCount; ++i)
  {
    if (interfaces[i].interfaceClass == 0x03)
    {
      ++hidCount;
    }
    else if (interfaces[i].interfaceClass == 0x01)
    {
      ++audioCount;
    }
    if (interfaces[i].claimAttempted &&
        interfaces[i].claimResult != ESP_OK)
    {
      claimOk = 0;
    }
  }

  EspUsbHostEndpointInfo endpoints[12];
  const size_t endpointCount = usb.getEndpoints(address, endpoints, 12);
  uint8_t duplicate = 0;
  for (size_t i = 0; i < endpointCount; ++i)
  {
    for (size_t j = i + 1; j < endpointCount; ++j)
    {
      if (endpoints[i].address == endpoints[j].address)
      {
        duplicate = 1;
      }
    }
  }

  Serial.printf(
      "HOST_ENUM pid=%04x ifcount=%u eps=%u dup=%u hid=%u audio=%u claimok=%u\n",
      DEVICE_PID, static_cast<unsigned>(interfaceCount),
      static_cast<unsigned>(endpointCount), duplicate, hidCount,
      audioCount, claimOk);
}

static void fillOutputSamples()
{
  static int16_t value = 0;
  for (size_t i = 0;
       i < sizeof(outputSamples) / sizeof(outputSamples[0]); ++i)
  {
    outputSamples[i] = value;
    value += 257;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          if (device.pid != DEVICE_PID)
                          {
                            return;
                          }
                          deviceAddress = device.address;
                          Serial.printf(
                              "HOST_CONNECTED vid=%04x pid=%04x ifcount=%u\n",
                              device.vid, device.pid,
                              device.configurationInterfaceCount);

                          EspUsbHostAudioStreamInfo streams
                              [ESP_USB_HOST_MAX_AUDIO_STREAMS];
                          const size_t count = usb.getAudioStreams(
                              device.address, streams,
                              ESP_USB_HOST_MAX_AUDIO_STREAMS);
                          for (size_t i = 0; i < count; ++i)
                          {
                            Serial.printf(
                                "AUDIO_STREAM iface=%u alt=%u ep=0x%02x dir=%s channels=%u bytes=%u bits=%u rate=%lu maxPacket=%u\n",
                                streams[i].interfaceNumber,
                                streams[i].alternate,
                                streams[i].endpointAddress,
                                streams[i].input ? "IN" : "OUT",
                                streams[i].channels,
                                streams[i].bytesPerSample,
                                streams[i].bitsPerSample,
                                static_cast<unsigned long>(
                                    streams[i].sampleRate),
                                streams[i].maxPacketSize);
                          } });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
                   if (event.pressed && event.ascii)
                   {
                     Serial.printf("KEY %c\n",
                                   static_cast<char>(event.ascii));
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
    if (command == 'e')
    {
      reportEnumeration();
    }
    else if (command == 'i')
    {
      const uint32_t start = millis();
      while (!(deviceAddress != 0 &&
               usb.audioOutputReady(deviceAddress)) &&
             millis() - start < 15000)
      {
        delay(50);
      }
      Serial.printf("HOST_AUDIO addr=%u ready=%u\n", deviceAddress,
                    usb.audioOutputReady(deviceAddress) ? 1 : 0);
    }
    else if (command == 'a')
    {
      Serial.printf(
          "AUDIO_START %u\n",
          usb.audioOutputStart(1, 16, 48000, deviceAddress) ? 1 : 0);
    }
    else if (command == 's')
    {
      fillOutputSamples();
      const bool sent = usb.audioSend(
          reinterpret_cast<const uint8_t *>(outputSamples),
          sizeof(outputSamples), deviceAddress);
      Serial.printf("AUDIO_TX %u\n", sent ? 1 : 0);
    }
  }
  delay(1);
}
