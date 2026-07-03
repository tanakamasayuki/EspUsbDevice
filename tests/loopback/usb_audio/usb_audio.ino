#include "EspUsbDevice.h"
#include "EspUsbHost.h"

EspUsbDevice device;
EspUsbDeviceAudioSink audio(device,
                            48000,
                            ESP_USB_DEVICE_AUDIO_BITS_16,
                            ESP_USB_DEVICE_AUDIO_SPK_MONO,
                            ESP_USB_DEVICE_AUDIO_MIC_NONE);
EspUsbHost usb;

static volatile bool deviceConnected = false;
static volatile bool audioOutReady = false;
static volatile bool spkInterfaceEnabled = false;
static uint8_t audioAddress = 0;

static EspUsbHostAudioStreamInfo outStream;
static volatile bool outStreamFound = false;

static volatile uint32_t receivedAudioBytes = 0;
static int16_t outputSamples[480];

static bool waitFor(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while (!flag && millis() - start < timeoutMs)
  {
    delay(10);
  }
  return flag;
}

static bool waitReceivedBytes(uint32_t minBytes, uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while (millis() - start < timeoutMs)
  {
    if (receivedAudioBytes >= minBytes)
    {
      return true;
    }
    delay(10);
  }
  return false;
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

static void audioEventCallback(const EspUsbDeviceAudioEvent &event)
{
  if (event.type == ESP_USB_DEVICE_AUDIO_EVENT_INTERFACE)
  {
    const bool spk = event.interface != ESP_USB_DEVICE_AUDIO_INTERFACE_MIC;
    Serial.printf("AUDIO_INTERFACE %s %u\n", spk ? "SPK" : "MIC", event.enabled ? 1 : 0);
    if (spk && event.enabled)
    {
      spkInterfaceEnabled = true;
    }
  }
}

static void audioDataCallback(void *data, uint16_t length)
{
  audio.applyVolume(data, length);
  receivedAudioBytes += length;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_usb_audio");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x class=0x%02x speed=%u\n",
                                        deviceInfo.vid,
                                        deviceInfo.pid,
                                        deviceInfo.deviceClass,
                                        static_cast<unsigned>(deviceInfo.speed));
                          deviceConnected = true;
                          if (usb.audioOutputReady(deviceInfo.address))
                          {
                            audioAddress = deviceInfo.address;
                            audioOutReady = true;
                            Serial.printf("AUDIO_OUT_READY addr=%u\n", deviceInfo.address);
                          }

                          EspUsbHostAudioStreamInfo audioStreams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
                          const size_t audioStreamCount = usb.getAudioStreams(deviceInfo.address, audioStreams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
                          for (size_t i = 0; i < audioStreamCount; i++)
                          {
                            Serial.printf("AUDIO_STREAM iface=%u alt=%u ep=0x%02x dir=%s channels=%u bytes=%u bits=%u rate=%lu maxPacket=%u interval=%u\n",
                                          audioStreams[i].interfaceNumber,
                                          audioStreams[i].alternate,
                                          audioStreams[i].endpointAddress,
                                          audioStreams[i].input ? "IN" : "OUT",
                                          audioStreams[i].channels,
                                          audioStreams[i].bytesPerSample,
                                          audioStreams[i].bitsPerSample,
                                          static_cast<unsigned long>(audioStreams[i].sampleRate),
                                          audioStreams[i].maxPacketSize,
                                          audioStreams[i].interval);
                            if (!audioStreams[i].input && audioStreams[i].alternate != 0 && !outStreamFound)
                            {
                              outStream = audioStreams[i];
                              outStreamFound = true;
                            }
                          } });

  EspUsbHostConfig hostConfig;
  hostConfig.port = ESP_USB_HOST_PORT_HIGH_SPEED;
  if (!usb.begin(hostConfig))
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("HOST_READY hs");

  audio.onEvent(audioEventCallback);
  audio.onData(audioDataCallback);

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.port = ESP_USB_DEVICE_PORT_HIGH_SPEED;
  deviceConfig.speed = ESP_USB_DEVICE_SPEED_HIGH;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4021;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback USB Audio";
  deviceConfig.serialNumber = "espusb-loopback-usb-audio";

  if (!device.begin(deviceConfig))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("DEVICE_READY hs");

  if (!waitFor(deviceConnected, 30000))
  {
    Serial.printf("DEVICE_TIMEOUT host_error=%s device_error=%s\n", usb.lastErrorName(), device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }

  if (!waitFor(audioOutReady, 5000) || !outStreamFound)
  {
    Serial.printf("AUDIO_OUT_NOT_READY ready=%u stream=%u\n", audioOutReady ? 1 : 0, outStreamFound ? 1 : 0);
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }

  delay(500);

  bool ok = true;

  const bool started = usb.audioOutputStart(outStream, 48000, audioAddress);
  Serial.printf("AUDIO_START %u\n", started ? 1 : 0);
  ok = ok && started;
  ok = ok && waitFor(spkInterfaceEnabled, 3000);

  receivedAudioBytes = 0;
  Serial.println("AUDIO_RESET");

  const uint32_t txStart = millis();
  uint32_t txBytes = 0;
  while (millis() - txStart < 3000 && receivedAudioBytes < 960)
  {
    fillOutputSamples();
    if (usb.audioSend(reinterpret_cast<const uint8_t *>(outputSamples), sizeof(outputSamples), audioAddress))
    {
      txBytes += sizeof(outputSamples);
    }
    delay(10);
  }
  Serial.printf("AUDIO_TX %lu\n", static_cast<unsigned long>(txBytes));

  const bool received = waitReceivedBytes(960, 3000);
  Serial.printf("DEVICE_RX_AUDIO %lu\n", static_cast<unsigned long>(receivedAudioBytes));
  ok = ok && received;

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
