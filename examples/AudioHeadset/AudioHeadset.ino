#include "EspUsbDevice.h"

// USB Audio headset: a single device that is BOTH a speaker (host -> device)
// and a microphone (device -> host) at the same time. On the host it appears
// as one playback device and one recording device.
//
// This example is a loopback headset: whatever the host plays to the speaker is
// echoed straight back on the microphone. Play audio to the "EspUsbDevice
// Headset" output and record its input to hear the same signal returned.
//
// The same EspUsbDeviceAudio class covers every direction; a headset is just a
// speaker channel layout plus a microphone channel layout. Here both are mono
// 48 kHz / 16-bit so the received speaker PCM can be written back to the mic
// unchanged.

EspUsbDevice device;
EspUsbDeviceAudio audio(device,
                        48000,
                        ESP_USB_DEVICE_AUDIO_BITS_16,
                        ESP_USB_DEVICE_AUDIO_SPK_MONO,
                        ESP_USB_DEVICE_AUDIO_MIC_MONO);

static void onAudioEvent(const EspUsbDeviceAudioEvent &event)
{
  if (event.type == ESP_USB_DEVICE_AUDIO_EVENT_INTERFACE)
  {
    Serial.printf("AUDIO_INTERFACE %s %u\n",
                  event.interface == ESP_USB_DEVICE_AUDIO_INTERFACE_MIC ? "mic" : "speaker",
                  event.enabled ? 1 : 0);
  }
}

static void onSpeakerData(void *data, uint16_t length)
{
  // Runs on the audio receive task. Apply the host's volume/mute to the
  // playback samples, then echo them straight back to the host as microphone
  // input. writeMic() only accepts what the USB IN FIFO has room for (0 while
  // the host is not recording), so the echo naturally follows the stream rate.
  audio.applyVolume(data, length);
  audio.writeMic(data, length);
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  audio.onEvent(onAudioEvent);
  audio.onData(onSpeakerData);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4024;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Headset";
  config.serialNumber = "espusb-headset";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.printf("Headset ready: %lu Hz, %u-bit, %u spk / %u mic channel(s)\n",
                static_cast<unsigned long>(audio.sampleRate()),
                audio.bitsPerSample(),
                audio.speakerChannels(),
                audio.micChannels());
}

void loop()
{
  // Nothing to do here: playback data arrives on the audio receive task and is
  // echoed back to the microphone from onSpeakerData().
  delay(10);
}
