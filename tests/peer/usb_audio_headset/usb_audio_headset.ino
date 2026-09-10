#include "EspUsbHost.h"
#include <stdlib.h>

// Host side of the USB Audio headset peer test: one device that is both a
// speaker (OUT) and a microphone (IN). Start both streams, send speaker PCM
// (host -> device) and confirm the device receives it, and confirm the mic
// stream (device -> host) arrives and is non-silent.

EspUsbHost usb;

static volatile uint8_t audioAddress = 0;
static volatile uint32_t rxBytes = 0;
static volatile int32_t rxMaxAbs = 0;
static int16_t outputSamples[480];

// The stream report is a function rather than only a connect-time announcement,
// so a test can ask for it. It also latches the audio address for any device
// that reports streams at all, which is what the command handlers act on.
static void reportAudioStreams(uint8_t address)
{
  EspUsbHostAudioStreamInfo audioStreams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
  const size_t audioStreamCount = usb.getAudioStreams(address, audioStreams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
  if (audioStreamCount > 0)
  {
    audioAddress = address;
  }
  Serial.printf("AUDIO_STREAMS count=%u\n", static_cast<unsigned>(audioStreamCount));
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
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.printf("DEVICE_CONNECTED addr=%u class=0x%02x\n", device.address, device.deviceClass);

                          reportAudioStreams(device.address); });

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

static void fillOutputSamples()
{
  static int16_t value = 0;
  for (size_t i = 0; i < sizeof(outputSamples) / sizeof(outputSamples[0]); i++)
  {
    outputSamples[i] = value;
    value += 257;
  }
}

// Block until the peer has been enumerated, so every command below answers about
// a device that is actually attached, whatever order the tests run in.
//
// audioAddress is latched in onDeviceConnected, which fires after the host has
// claimed the interfaces - the right side of the event for anything that reads
// the device's interfaces or endpoints. Waiting here rather than announcing once
// at boot is what lets a test run in any position: a boot announcement is only
// visible to whichever test happens to be first.
static bool waitForDevice(uint32_t timeoutMs = 5000)
{
  const uint32_t startedAt = millis();
  while (audioAddress == 0 && millis() - startedAt < timeoutMs)
  {
    delay(10);
  }
  return audioAddress != 0;
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    waitForDevice();
    if (command == 'S')
    {
      reportAudioStreams(audioAddress);
    }
    else if (command == 'i')
    {
      // Wait for a stable device that is ready in BOTH directions (it can
      // re-enumerate a few times at startup). Poll up to 15 s so the test
      // synchronizes regardless of run order or boot timing.
      const uint32_t start = millis();
      while (!(audioAddress != 0 && usb.audioOutputReady(audioAddress) && usb.audioInputReady(audioAddress)) && millis() - start < 15000)
      {
        delay(50);
      }
      Serial.printf("HOST_AUDIO addr=%u out=%u in=%u\n",
                    audioAddress,
                    usb.audioOutputReady(audioAddress) ? 1 : 0,
                    usb.audioInputReady(audioAddress) ? 1 : 0);
    }
    else if (command == 'a')
    {
      const bool out = usb.audioOutputStart(1, 16, 48000, audioAddress);
      const bool in = usb.audioInputStart(1, 16, 48000, audioAddress);
      Serial.printf("HEADSET_START out=%u in=%u\n", out ? 1 : 0, in ? 1 : 0);
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
    else if (command == 'r')
    {
      rxBytes = 0;
      rxMaxAbs = 0;
      Serial.println("HEADSET_RESET");
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
