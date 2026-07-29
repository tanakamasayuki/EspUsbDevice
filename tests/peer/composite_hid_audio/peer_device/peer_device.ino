#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbAudioFunction audio(device);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();

static bool beginOk = false;
static const char *beginError = "ESP_OK";
static uint32_t receivedAudioBytes = 0;

static bool tapKeyWithRetry(char key)
{
  const uint32_t start = millis();
  while (millis() - start < 1000)
  {
    if (keyboard.tapKey(key))
    {
      return true;
    }
    delay(5);
  }
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  playback.addFormat({48000, 1, 2, 16});

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4027;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice HID+UAC1";
  config.serialNumber = "espusb-hid-audio";

  beginOk = device.begin(config);
  beginError = device.lastErrorName();
  Serial.printf("DEVICE_BEGIN %s %s\n", beginOk ? "ok" : "ng",
                beginError);
}

void loop()
{
  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    if (command == 'b')
    {
      Serial.printf("DEVICE_BEGIN %s %s\n", beginOk ? "ok" : "ng",
                    beginError);
    }
    else if (command == 'k')
    {
      Serial.printf("DEVICE_KEY %u\n",
                    tapKeyWithRetry('a') ? 1 : 0);
    }
    else if (command == 'r')
    {
      receivedAudioBytes = 0;
      playback.resetStats();
      Serial.println("DEVICE_AUDIO_RESET");
    }
  }

  EspUsbAudioEvent event;
  while (audio.pollEvent(event))
  {
    if (event.type == EspUsbAudioEventType::StreamStateChanged)
    {
      Serial.printf("AUDIO_INTERFACE PLAYBACK %u alt=%u\n",
                    event.enabled ? 1 : 0, event.alternateSetting);
    }
  }

  uint8_t pcm[256];
  receivedAudioBytes += playback.read(pcm, sizeof(pcm));
  if (receivedAudioBytes >= 96)
  {
    Serial.printf("DEVICE_RX_AUDIO %lu\n",
                  static_cast<unsigned long>(receivedAudioBytes));
    receivedAudioBytes = 0;
  }
  delay(1);
}
