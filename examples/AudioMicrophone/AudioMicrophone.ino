#include "EspUsbDevice.h"
#include <math.h>

// USB Audio "source" (microphone): the device sends PCM to the host. This
// exposes a mono 48 kHz / 16-bit recording device that streams a 440 Hz sine
// tone. Select it as a recording/input device on the host to hear the tone.
//
// The same EspUsbDeviceAudio class handles both directions: speaker
// channels for host -> device (sink) and microphone channels for device ->
// host (source). Here speaker is NONE and microphone is MONO.

EspUsbDevice device;
EspUsbDeviceAudio audio(device,
                            48000,
                            ESP_USB_DEVICE_AUDIO_BITS_16,
                            ESP_USB_DEVICE_AUDIO_SPK_NONE,
                            ESP_USB_DEVICE_AUDIO_MIC_MONO);

static const float kToneHz = 440.0f;
static float phase = 0.0f;
static float phaseInc = 0.0f;

static void onAudioEvent(const EspUsbDeviceAudioEvent &event)
{
  if (event.type == ESP_USB_DEVICE_AUDIO_EVENT_INTERFACE)
  {
    Serial.printf("AUDIO_INTERFACE %s %u\n",
                  event.interface == ESP_USB_DEVICE_AUDIO_INTERFACE_MIC ? "mic" : "speaker",
                  event.enabled ? 1 : 0);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  phaseInc = 2.0f * static_cast<float>(M_PI) * kToneHz / static_cast<float>(audio.sampleRate());
  audio.onEvent(onAudioEvent);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4022;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Microphone";
  config.serialNumber = "espusb-microphone";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.printf("Microphone ready: %lu Hz, %u-bit, %u channel mic\n",
                static_cast<unsigned long>(audio.sampleRate()),
                audio.bitsPerSample(),
                audio.micChannels());
}

void loop()
{
  // Generate ~1 ms of the sine tone and push it toward the host. writeMic()
  // accepts as many bytes as the USB IN FIFO has room for; while the host is not
  // recording the FIFO stays full and writeMic() returns 0, which naturally
  // throttles generation.
  static int16_t samples[48];  // 1 ms at 48 kHz mono
  for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
  {
    samples[i] = static_cast<int16_t>(sinf(phase) * 8000.0f);
    phase += phaseInc;
    if (phase >= 2.0f * static_cast<float>(M_PI))
    {
      phase -= 2.0f * static_cast<float>(M_PI);
    }
  }
  audio.writeMic(samples, sizeof(samples));
  delay(1);
}
