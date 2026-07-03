#include <EspUsbDevice.h>
#include <M5Unified.h>
#include <PCMFlowDeviceM5.h>

// USB Audio headset backed by M5 hardware: host audio is played on the M5
// speaker (host -> device) while the M5 microphone is streamed back to the host
// (device -> host).
//
// KNOWN LIMITATION -- running the M5 speaker and microphone at the same time is
// not reliably supported. M5Unified drives M5.Speaker and M5.Mic through a
// single I2S port installed TX-only or RX-only, and each begin() uninstalls the
// other's driver, so full-duplex playback+capture glitches / is unstable. This
// was confirmed on CoreS3 as well; forcing the speaker onto a second I2S port
// did not help. Treat this example as a best-effort demo. For reliable audio,
// use the single-direction examples instead: AudioSpeakerM5 (playback) and
// AudioMicrophoneM5 (capture). The non-M5 AudioHeadset example shows both
// directions over USB without touching M5 audio hardware.
//
// A USB Audio device in this stack uses one sample rate for both directions, so
// speaker and microphone share a single 48 kHz / 16-bit format (stereo speaker,
// mono microphone).

static constexpr uint32_t kSampleRate = 48000;
static constexpr size_t kMaxPlayFrames = (kSampleRate * 80u) / 1000u;
static constexpr size_t kBlockSamples = 480;  // 10 ms at 48 kHz mono
static constexpr size_t kRingBlocks = 3;       // >= 3 so the oldest block is complete

EspUsbDevice device;
EspUsbDeviceAudio audio(device,
                        kSampleRate,
                        ESP_USB_DEVICE_AUDIO_BITS_16,
                        ESP_USB_DEVICE_AUDIO_SPK_STEREO,
                        ESP_USB_DEVICE_AUDIO_MIC_MONO);

M5SpeakerBufferedPlayer<kMaxPlayFrames> player;

static int16_t micRing[kRingBlocks][kBlockSamples];
static size_t recIdx = 0;
static volatile bool micEnabled = false;
static volatile bool speakerEnabled = false;

static void onSpeakerPcm(const EspUsbDeviceAudioPcm &pcm)
{
  // Host -> device: play the received PCM on the M5 speaker.
  if (!pcm.data || pcm.length == 0)
  {
    return;
  }
  audio.applyVolume(pcm.data, pcm.length);
  const PCMFormat format{pcm.sampleRate, pcm.channels, static_cast<uint8_t>(pcm.bytesPerSample * 8u)};
  player.writePcm(pcm.data, pcm.length, format);
}

static void onAudioEvent(const EspUsbDeviceAudioEvent &event)
{
  if (event.type != ESP_USB_DEVICE_AUDIO_EVENT_INTERFACE)
  {
    return;
  }
  if (event.interface == ESP_USB_DEVICE_AUDIO_INTERFACE_SPEAKER)
  {
    speakerEnabled = event.enabled;
    if (!event.enabled)
    {
      player.stop();
    }
    Serial.printf("AUDIO_INTERFACE speaker %u\n", event.enabled ? 1 : 0);
  }
  else
  {
    micEnabled = event.enabled;
    Serial.printf("AUDIO_INTERFACE mic %u\n", event.enabled ? 1 : 0);
  }
}

static int rateLineY = 0;
static uint32_t shownRate = 0xFFFFFFFFu;

// Draw the current USB Audio sample rate on a fixed screen line, repainting only
// when it changes. Speaker and microphone share this one rate. The fixed-width
// field overwrites any previous digits.
static void showSampleRate(uint32_t rate)
{
  if (rate == shownRate)
  {
    return;
  }
  shownRate = rate;
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(0, rateLineY);
  M5.Display.printf("Rate:%6lu Hz", static_cast<unsigned long>(rate));
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
  M5.Display.println("AudioHeadset M5");

  M5.Speaker.begin();
  M5.Speaker.setVolume(128);
  M5.Mic.setSampleRate(kSampleRate);
  M5.Mic.begin();

  if (!player.begin({kSampleRate, 1, 16},
                    M5SpeakerBufferedPlayer<kMaxPlayFrames>::stableProfile()))
  {
    Serial.println("PLAYER_BEGIN_FAILED");
    M5.Display.println("Player failed");
    return;
  }

  audio.onPcm(onSpeakerPcm);
  audio.onEvent(onAudioEvent);

  EspUsbDeviceConfig usb;
  usb.vid = 0x303a;
  usb.pid = 0x4024;
  usb.manufacturer = "EspUsb";
  usb.product = "EspUsbDevice M5 Headset";
  usb.serialNumber = "espusb-m5-headset";

  if (!device.begin(usb))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    M5.Display.println("USB failed");
    return;
  }

  Serial.println("AudioHeadsetM5 ready");
  M5.Display.println("USB headset ready");
  rateLineY = M5.Display.getCursorY();
  showSampleRate(audio.sampleRate());
}

void loop()
{
  M5.update();
  showSampleRate(audio.sampleRate());

  // Device -> host: enqueue a mic capture block and, when accepted, push the
  // oldest (completed) block to the host.
  if (micEnabled && M5.Mic.isEnabled() && M5.Mic.record(micRing[recIdx], kBlockSamples, kSampleRate))
  {
    const size_t doneIdx = (recIdx + 1) % kRingBlocks;
    audio.writeMic(micRing[doneIdx], kBlockSamples * sizeof(int16_t));
    recIdx = (recIdx + 1) % kRingBlocks;
  }
  delay(1);
}
