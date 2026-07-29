#include "EspUsbDevice.h"

// USB Audio source (microphone): device -> host PCM. Generates a loud, varying
// sawtooth so the host can confirm it received real, non-silent audio.

EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();

static uint32_t micTxBytes = 0;
static int16_t genValue = 0;
static bool captureEnabled = false;

void setup()
{
  Serial.begin(115200);
  delay(5000);

  capture.addFormat({48000, 1, 2, 16});

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4023;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Microphone Peer";
  config.serialNumber = "espusb-mic-peer";

  const bool ok = device.begin(config);
  Serial.printf("MIC_DEVICE_READY %u error=%s\n", ok ? 1 : 0, device.lastErrorName());
}

void loop()
{
  EspUsbAudioEvent event;
  while (audio.pollEvent(event))
  {
    if (event.type == EspUsbAudioEventType::StreamStateChanged &&
        event.target == EspUsbAudioEventTarget::Capture)
    {
      captureEnabled = event.enabled;
      Serial.printf("AUDIO_INTERFACE MIC %u\n",
                    captureEnabled ? 1 : 0);
    }
  }

  if (captureEnabled)
  {
    int16_t samples[48];
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
    {
      samples[i] = genValue;
      genValue = static_cast<int16_t>(genValue + 1024);
    }
    micTxBytes += capture.write(samples, sizeof(samples));
  }

  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    if (command == '?')
    {
      const EspUsbAudioStreamStats stats = capture.stats();
      Serial.printf(
          "MIC_ALIVE tx=%lu usb=%lu underruns=%lu overruns=%lu events=%lu\n",
          static_cast<unsigned long>(micTxBytes),
          static_cast<unsigned long>(stats.transferredBytes),
          static_cast<unsigned long>(stats.underrunCount),
          static_cast<unsigned long>(stats.overrunCount),
          static_cast<unsigned long>(audio.droppedEvents()));
    }
  }
  delay(1);
}
