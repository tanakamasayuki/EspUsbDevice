#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceAudioSink audio(device,
                            48000,
                            ESP_USB_DEVICE_AUDIO_BITS_16,
                            ESP_USB_DEVICE_AUDIO_SPK_MONO,
                            ESP_USB_DEVICE_AUDIO_MIC_NONE);

static uint32_t receivedAudioBytes = 0;
static bool receivedAudioReported = false;
static volatile uint32_t volumeEventCount = 0;
static volatile uint32_t muteEventCount = 0;

static void audioEventCallback(const EspUsbDeviceAudioEvent &event)
{
  if (event.type == ESP_USB_DEVICE_AUDIO_EVENT_INTERFACE)
  {
    Serial.printf("AUDIO_INTERFACE %s %u\n",
                  event.interface == ESP_USB_DEVICE_AUDIO_INTERFACE_MIC ? "MIC" : "SPK",
                  event.enabled ? 1 : 0);
  }
  else if (event.type == ESP_USB_DEVICE_AUDIO_EVENT_VOLUME)
  {
    // A real app often does visible work per volume event (log / update UI).
    // Keep it lightweight but non-trivial so consumer pressure is realistic.
    volumeEventCount++;
    Serial.printf("DEV_VOL ch=%u db=%d n=%lu\n",
                  static_cast<unsigned>(event.channel),
                  event.volumeDb,
                  static_cast<unsigned long>(volumeEventCount));
  }
  else if (event.type == ESP_USB_DEVICE_AUDIO_EVENT_MUTE)
  {
    muteEventCount++;
    Serial.printf("DEV_MUTE ch=%u m=%u n=%lu\n",
                  static_cast<unsigned>(event.channel),
                  event.muted ? 1 : 0,
                  static_cast<unsigned long>(muteEventCount));
  }
}

static void audioDataCallback(void *data, uint16_t length)
{
  audio.applyVolume(data, length);
  receivedAudioBytes += length;
  if (!receivedAudioReported && receivedAudioBytes >= 96)
  {
    receivedAudioReported = true;
    Serial.printf("DEVICE_RX_AUDIO %lu\n", static_cast<unsigned long>(receivedAudioBytes));
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
  config.pid = 0x4021;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Audio Peer";
  config.serialNumber = "espusb-audio-peer";

  const bool ok = device.begin(config);
  Serial.printf("AUDIO_DEVICE_READY %u error=%s\n", ok ? 1 : 0, device.lastErrorName());
}

void loop()
{
  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    if (command == 'r')
    {
      receivedAudioBytes = 0;
      receivedAudioReported = false;
      Serial.println("DEVICE_AUDIO_RESET");
    }
    else if (command == '?')
    {
      // Liveness probe. If the device rebooted during a volume flood, setup()
      // reruns and prints AUDIO_DEVICE_READY again; a healthy device answers
      // here with its accumulated event counts and never reset.
      Serial.printf("DEVICE_ALIVE vol=%lu mute=%lu\n",
                    static_cast<unsigned long>(volumeEventCount),
                    static_cast<unsigned long>(muteEventCount));
    }
  }
  delay(1);
}
