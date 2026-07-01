#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceAudioSink audio(device,
                            48000,
                            ESP_USB_DEVICE_AUDIO_BITS_16,
                            ESP_USB_DEVICE_AUDIO_SPK_STEREO,
                            ESP_USB_DEVICE_AUDIO_MIC_NONE);

static volatile uint32_t audioChunks = 0;
static volatile uint32_t audioBytes = 0;
static volatile uint16_t lastChunkLength = 0;
static volatile int16_t lastLeftSample = 0;
static volatile int16_t lastRightSample = 0;
static uint32_t lastLogMs = 0;

static void onAudioData(void *data, uint16_t length)
{
  audio.applyVolume(data, length);
  audioChunks++;
  audioBytes += length;
  lastChunkLength = length;

  if (data && length >= 4)
  {
    const int16_t *samples = static_cast<const int16_t *>(data);
    lastLeftSample = samples[0];
    lastRightSample = samples[1];
  }
}

static void onAudioEvent(const EspUsbDeviceAudioEvent &event)
{
  if (event.type == ESP_USB_DEVICE_AUDIO_EVENT_INTERFACE)
  {
    Serial.printf("AUDIO_INTERFACE %s %u\n",
                  event.interface == ESP_USB_DEVICE_AUDIO_INTERFACE_MIC ? "mic" : "speaker",
                  event.enabled ? 1 : 0);
  }
  else if (event.type == ESP_USB_DEVICE_AUDIO_EVENT_SAMPLE_RATE)
  {
    Serial.printf("AUDIO_RATE %lu\n", static_cast<unsigned long>(event.sampleRate));
  }
  else if (event.type == ESP_USB_DEVICE_AUDIO_EVENT_VOLUME)
  {
    Serial.printf("AUDIO_VOLUME ch=%u db=%d\n", event.channel, event.volumeDb);
  }
  else if (event.type == ESP_USB_DEVICE_AUDIO_EVENT_MUTE)
  {
    Serial.printf("AUDIO_MUTE ch=%u muted=%u\n", event.channel, event.muted ? 1 : 0);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  audio.onData(onAudioData);
  audio.onEvent(onAudioEvent);

  EspUsbDeviceConfig config;
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4020;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Audio Sink";
  config.serialNumber = "espusb-audio-sink";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.printf("Audio sink ready: %lu Hz, %u-bit, %u channel speaker\n",
                static_cast<unsigned long>(audio.sampleRate()),
                audio.bitsPerSample(),
                audio.speakerChannels());
}

void loop()
{
  const uint32_t now = millis();
  if (now - lastLogMs >= 1000)
  {
    lastLogMs = now;
    noInterrupts();
    const uint32_t chunks = audioChunks;
    const uint32_t bytes = audioBytes;
    const uint16_t chunkLength = lastChunkLength;
    const int16_t left = lastLeftSample;
    const int16_t right = lastRightSample;
    audioChunks = 0;
    audioBytes = 0;
    interrupts();

    Serial.printf("AUDIO_RX chunks=%lu bytes=%lu last=%u sample_l=%d sample_r=%d\n",
                  static_cast<unsigned long>(chunks),
                  static_cast<unsigned long>(bytes),
                  chunkLength,
                  left,
                  right);
  }
  delay(1);
}
