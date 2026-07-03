#include "EspUsbDevice.h"

// USB Audio source (microphone): device -> host PCM. Generates a loud, varying
// sawtooth so the host can confirm it received real, non-silent audio.

EspUsbDevice device;
EspUsbDeviceAudioSink audio(device,
                            48000,
                            ESP_USB_DEVICE_AUDIO_BITS_16,
                            ESP_USB_DEVICE_AUDIO_SPK_NONE,
                            ESP_USB_DEVICE_AUDIO_MIC_MONO);

static volatile uint32_t micTxBytes = 0;
static int16_t genValue = 0;

static void audioEventCallback(const EspUsbDeviceAudioEvent &event)
{
  if (event.type == ESP_USB_DEVICE_AUDIO_EVENT_INTERFACE)
  {
    Serial.printf("AUDIO_INTERFACE %s %u\n",
                  event.interface == ESP_USB_DEVICE_AUDIO_INTERFACE_MIC ? "MIC" : "SPK",
                  event.enabled ? 1 : 0);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  audio.onEvent(audioEventCallback);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4023;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Microphone Peer";
  config.serialNumber = "espusb-mic-peer";

  const bool ok = device.begin(config);
  Serial.printf("MIC_DEVICE_READY %u error=%s\n", ok ? 1 : 0, device.lastErrorName());
}

void loop()
{
  // Generate ~1 ms of a sawtooth and push it toward the host. writeMic() accepts
  // as many bytes as the USB IN FIFO has room for (0 while the host is not
  // recording), which throttles generation to the streaming rate.
  int16_t samples[48];
  for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
  {
    samples[i] = genValue;
    genValue = static_cast<int16_t>(genValue + 1024);
  }
  const uint16_t written = audio.writeMic(samples, sizeof(samples));
  if (written > 0)
  {
    micTxBytes += written;
  }

  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    if (command == '?')
    {
      Serial.printf("MIC_ALIVE tx=%lu\n", static_cast<unsigned long>(micTxBytes));
    }
  }
  delay(1);
}
