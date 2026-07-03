#include <EspUsbDevice.h>
#include <M5Unified.h>

// USB Audio microphone (device -> host) backed by the M5 built-in microphone.
// The device captures PCM from M5.Mic and streams it to the host, so it appears
// as a recording/input device on the PC.
//
// M5 PDM microphones run comfortably at 16 kHz mono, so the USB Audio device is
// declared at that rate. M5.Mic.record() is asynchronous and double-buffered:
// it fills the buffer you pass in the background, so this sketch keeps a small
// ring of blocks and sends the oldest (already-completed) block to the host.

static constexpr uint32_t kSampleRate = 16000;
static constexpr size_t kBlockSamples = 240;  // 15 ms at 16 kHz mono
static constexpr size_t kRingBlocks = 3;       // >= 3 so the oldest block is complete

EspUsbDevice device;
EspUsbDeviceAudio audio(device,
                        kSampleRate,
                        ESP_USB_DEVICE_AUDIO_BITS_16,
                        ESP_USB_DEVICE_AUDIO_SPK_NONE,
                        ESP_USB_DEVICE_AUDIO_MIC_MONO);

static int16_t micRing[kRingBlocks][kBlockSamples];
static size_t recIdx = 0;
static volatile bool micEnabled = false;
static volatile uint32_t micTxBytes = 0;
static uint32_t lastLogMs = 0;

static void onAudioEvent(const EspUsbDeviceAudioEvent &event)
{
  if (event.type == ESP_USB_DEVICE_AUDIO_EVENT_INTERFACE && event.interface == ESP_USB_DEVICE_AUDIO_INTERFACE_MIC)
  {
    micEnabled = event.enabled;
    Serial.printf("AUDIO_INTERFACE mic %u\n", event.enabled ? 1 : 0);
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
  M5.Display.println("AudioMicrophone M5");

  M5.Mic.setSampleRate(kSampleRate);
  if (!M5.Mic.begin())
  {
    Serial.println("MIC_BEGIN_FAILED");
    M5.Display.println("Mic failed");
    return;
  }

  audio.onEvent(onAudioEvent);

  EspUsbDeviceConfig usb;
  usb.vid = 0x303a;
  usb.pid = 0x4022;
  usb.manufacturer = "EspUsb";
  usb.product = "EspUsbDevice M5 Microphone";
  usb.serialNumber = "espusb-m5-microphone";

  if (!device.begin(usb))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    M5.Display.println("USB failed");
    return;
  }

  Serial.println("AudioMicrophoneM5 ready");
  M5.Display.println("USB mic ready");
}

void loop()
{
  M5.update();

  // Enqueue the next capture block. When record() accepts it, the oldest block
  // in the ring (kRingBlocks-1 iterations back) is complete; push that to the
  // host. writeMic() returns 0 while the host is not recording, which is fine.
  if (M5.Mic.isEnabled() && M5.Mic.record(micRing[recIdx], kBlockSamples, kSampleRate))
  {
    const size_t doneIdx = (recIdx + 1) % kRingBlocks;
    if (micEnabled)
    {
      const uint16_t written = audio.writeMic(micRing[doneIdx], kBlockSamples * sizeof(int16_t));
      if (written > 0)
      {
        micTxBytes += written;
      }
    }
    recIdx = (recIdx + 1) % kRingBlocks;
  }

  const uint32_t now = millis();
  if (now - lastLogMs >= 1000)
  {
    lastLogMs = now;
    noInterrupts();
    const uint32_t tx = micTxBytes;
    micTxBytes = 0;
    interrupts();
    Serial.printf("AUDIO_M5_MIC enabled=%u tx_bytes=%lu\n", micEnabled ? 1 : 0, static_cast<unsigned long>(tx));
  }
  delay(1);
}
