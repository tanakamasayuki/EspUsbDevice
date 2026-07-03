#include "EspUsbDevice.h"

// USB Audio headset (speaker + microphone on one device). Verifies both
// directions at once: it counts speaker PCM received from the host (host ->
// device) and continuously streams a loud sawtooth to the host (device ->
// host). The two paths are independent so each direction can be checked on its
// own, regardless of the other's timing.

EspUsbDevice device;
EspUsbDeviceAudio audio(device,
                        48000,
                        ESP_USB_DEVICE_AUDIO_BITS_16,
                        ESP_USB_DEVICE_AUDIO_SPK_MONO,
                        ESP_USB_DEVICE_AUDIO_MIC_MONO);

static volatile uint32_t rxBytes = 0;
static bool rxReported = false;
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

static void audioDataCallback(void *data, uint16_t length)
{
  // Host -> device (speaker) path.
  audio.applyVolume(data, length);
  rxBytes += length;
  if (!rxReported && rxBytes >= 96)
  {
    rxReported = true;
    Serial.printf("DEVICE_RX_AUDIO %lu\n", static_cast<unsigned long>(rxBytes));
  }
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  audio.onEvent(audioEventCallback);
  audio.onData(audioDataCallback);

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
  // Device -> host (microphone) path: generate ~1 ms of a sawtooth and push it
  // toward the host. writeMic() returns 0 while the host is not recording, which
  // throttles generation to the streaming rate.
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
    if (command == 'r')
    {
      rxBytes = 0;
      rxReported = false;
      Serial.println("HEADSET_RESET");
    }
    else if (command == '?')
    {
      Serial.printf("HEADSET_ALIVE rx=%lu tx=%lu\n",
                    static_cast<unsigned long>(rxBytes),
                    static_cast<unsigned long>(micTxBytes));
    }
  }
  delay(1);
}
