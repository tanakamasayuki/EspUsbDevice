#include <EspUsbDevice.h>
#include <M5Unified.h>
#include <PCMFlowDeviceM5.h>

static constexpr uint32_t kSampleRate = 48000;
static constexpr size_t kMaxPlayFrames = (kSampleRate * 80u) / 1000u;

EspUsbDevice device;
EspUsbDeviceAudioSink audio(device,
                            kSampleRate,
                            ESP_USB_DEVICE_AUDIO_BITS_16,
                            ESP_USB_DEVICE_AUDIO_SPK_STEREO,
                            ESP_USB_DEVICE_AUDIO_MIC_NONE);

M5SpeakerBufferedPlayer<kMaxPlayFrames> player;

static volatile uint32_t audioChunks = 0;
static volatile uint32_t audioBytes = 0;
static volatile uint32_t speakerBytes = 0;
static volatile uint32_t unsupportedChunks = 0;
static uint32_t lastLogMs = 0;

static void onAudioPcm(const EspUsbDeviceAudioPcm &pcm)
{
  if (!pcm.data || pcm.length == 0)
  {
    return;
  }

  audio.applyVolume(pcm.data, pcm.length);
  audioChunks++;
  audioBytes += pcm.length;

  const PCMFormat format{pcm.sampleRate, pcm.channels, static_cast<uint8_t>(pcm.bytesPerSample * 8u)};
  const size_t written = player.writePcm(pcm.data, pcm.length, format);
  if (written == 0)
  {
    unsupportedChunks++;
    return;
  }
  speakerBytes += written;
}

static void onAudioEvent(const EspUsbDeviceAudioEvent &event)
{
  if (event.type == ESP_USB_DEVICE_AUDIO_EVENT_INTERFACE)
  {
    Serial.printf("AUDIO_INTERFACE %s %u\n",
                  event.interface == ESP_USB_DEVICE_AUDIO_INTERFACE_MIC ? "mic" : "speaker",
                  event.enabled ? 1 : 0);
    if (!event.enabled)
    {
      player.stop();
    }
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
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(1500);

  M5.Display.setTextSize(2);
  M5.Display.setCursor(0, 0);
  M5.Display.println("EspUsbDevice");
  M5.Display.println("AudioSink M5");

  M5.Speaker.begin();
  M5.Speaker.setVolume(128);

  if (!player.begin({kSampleRate, 1, 16},
                    M5SpeakerBufferedPlayer<kMaxPlayFrames>::stableProfile()))
  {
    Serial.println("PLAYER_BEGIN_FAILED");
    M5.Display.println("Player failed");
    return;
  }

  audio.onPcm(onAudioPcm);
  audio.onEvent(onAudioEvent);

  EspUsbDeviceConfig usb;
  usb.vid = 0x303a;
  usb.pid = 0x4021;
  usb.manufacturer = "EspUsb";
  usb.product = "EspUsbDevice M5 Speaker";
  usb.serialNumber = "espusb-m5-speaker";

  if (!device.begin(usb))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    M5.Display.println("USB failed");
    return;
  }

  Serial.println("AudioSinkM5Speaker ready");
  M5.Display.println("USB audio ready");
}

void loop()
{
  M5.update();

  const uint32_t now = millis();
  if (now - lastLogMs >= 1000)
  {
    lastLogMs = now;
    noInterrupts();
    const uint32_t chunks = audioChunks;
    const uint32_t bytes = audioBytes;
    const uint32_t speaker = speakerBytes;
    const uint32_t unsupported = unsupportedChunks;
    audioChunks = 0;
    audioBytes = 0;
    speakerBytes = 0;
    unsupportedChunks = 0;
    interrupts();

    Serial.printf("AUDIO_M5 chunks=%lu bytes=%lu speaker_bytes=%lu unsupported=%lu play_chunks=%lu waits=%lu gaps=%lu drops=%lu\n",
                  static_cast<unsigned long>(chunks),
                  static_cast<unsigned long>(bytes),
                  static_cast<unsigned long>(speaker),
                  static_cast<unsigned long>(unsupported),
                  static_cast<unsigned long>(player.chunks()),
                  static_cast<unsigned long>(player.waits()),
                  static_cast<unsigned long>(player.gapRisks()),
                  static_cast<unsigned long>(player.overflowDrops()));
  }
  delay(1);
}
