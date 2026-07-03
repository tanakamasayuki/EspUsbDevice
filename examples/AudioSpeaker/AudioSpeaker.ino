#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceAudio audio(device,
                            48000,
                            ESP_USB_DEVICE_AUDIO_BITS_16,
                            ESP_USB_DEVICE_AUDIO_SPK_STEREO,
                            ESP_USB_DEVICE_AUDIO_MIC_NONE);

static volatile uint32_t audioChunks = 0;
static volatile uint32_t audioBytes = 0;
static volatile uint16_t lastChunkLength = 0;
static volatile int16_t lastLeftSample = 0;
static volatile int16_t lastRightSample = 0;
static volatile uint32_t lastSampleRate = 0;
static volatile uint8_t lastChannels = 0;
static volatile uint8_t lastBytesPerSample = 0;
static uint32_t lastLogMs = 0;

static void onAudioPcm(const EspUsbDeviceAudioPcm &pcm)
{
  audio.applyVolume(pcm.data, pcm.length);
  audioChunks++;
  audioBytes += pcm.length;
  lastChunkLength = pcm.length;
  lastSampleRate = pcm.sampleRate;
  lastChannels = pcm.channels;
  lastBytesPerSample = pcm.bytesPerSample;

  if (pcm.data && pcm.length >= 4)
  {
    const int16_t *samples = static_cast<const int16_t *>(pcm.data);
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

  audio.onPcm(onAudioPcm);
  audio.onEvent(onAudioEvent);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4020;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Speaker";
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
    const uint32_t rate = lastSampleRate;
    const uint8_t channels = lastChannels;
    const uint8_t bytesPerSample = lastBytesPerSample;
    audioChunks = 0;
    audioBytes = 0;
    interrupts();

    Serial.printf("AUDIO_RX chunks=%lu bytes=%lu last=%u rate=%lu channels=%u bps=%u sample_l=%d sample_r=%d\n",
                  static_cast<unsigned long>(chunks),
                  static_cast<unsigned long>(bytes),
                  chunkLength,
                  static_cast<unsigned long>(rate),
                  channels,
                  bytesPerSample,
                  left,
                  right);
  }
  delay(1);
}
