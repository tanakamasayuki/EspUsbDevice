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

// Block until the host has configured us, so every command below answers from a
// usable device whatever order the tests run in.
//
// device.ready() is tud_mounted(): the host completed SET_CONFIGURATION. That is
// the event the tests actually depend on, and it is answered on demand rather
// than announced once at boot - a test that does not run first never sees an
// announcement, which is what made most of this suite fail when its modules were
// run in reverse. tests/peer/usb_msc has had this shape all along and was the
// only peer module that survived that check.
static bool waitForHost(uint32_t timeoutMs = 5000)
{
  const uint32_t startedAt = millis();
  while (!device.ready() && millis() - startedAt < timeoutMs)
  {
    device.task();
    delay(10);
  }
  return device.ready();
}

void loop()
{
  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    const bool hostReady = waitForHost();
    if (command == '?')
    {
      Serial.printf("DEVICE_READY %u\n", hostReady ? 1 : 0);
    }
    else if (command == 'b')
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
