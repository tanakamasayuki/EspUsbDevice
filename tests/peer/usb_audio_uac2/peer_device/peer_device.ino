#include "EspUsbDevice.h"

// UAC2 headset peer: speaker (host -> device) and microphone (device -> host)
// on one Audio Class 2.0 function.
//
// Unlike the UAC1 peers, this sketch also reports the device's own view of the
// control state - the sample rate the host programmed into the Clock Source, and
// the Feature Unit mute / volume for the master and the logical channel - so the
// test can check that what the host wrote is what the device applied, not just
// what the host reads back.

EspUsbDevice device;
EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();

static uint32_t rxBytes = 0;
static bool rxReported = false;
static uint32_t micTxBytes = 0;
static int16_t genValue = 0;
static bool captureEnabled = false;
static uint32_t volumeEventCount = 0;
static uint32_t muteEventCount = 0;
static uint32_t rateEventCount = 0;

void setup()
{
  Serial.begin(115200);
  delay(5000);

  // One format per stream: the descriptor builder emits a single alternate
  // setting per direction, so the Clock Source reports exactly this one rate.
  playback.addFormat({48000, 1, 2, 16});
  capture.addFormat({48000, 1, 2, 16});

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4025;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice UAC2 Peer";
  config.serialNumber = "espusb-uac2-peer";

  const bool ok = device.begin(config);
  Serial.printf("UAC2_DEVICE_READY %u proto=%s error=%s\n",
                ok ? 1 : 0,
                audio.protocol() == EspUsbAudioProtocol::Uac2 ? "uac2" : "uac1",
                device.lastErrorName());
}

void loop()
{
  uint8_t received[192];
  const size_t receivedLength = playback.read(received, sizeof(received));
  rxBytes += receivedLength;
  if (!rxReported && rxBytes >= 96)
  {
    rxReported = true;
    Serial.printf("DEVICE_RX_AUDIO %lu\n", static_cast<unsigned long>(rxBytes));
  }

  EspUsbAudioEvent event;
  while (audio.pollEvent(event))
  {
    if (event.type == EspUsbAudioEventType::StreamStateChanged)
    {
      if (event.target == EspUsbAudioEventTarget::Capture)
      {
        captureEnabled = event.enabled;
      }
      Serial.printf("AUDIO_INTERFACE %s %u alt=%u\n",
                    event.target == EspUsbAudioEventTarget::Playback ? "SPK" : "MIC",
                    event.enabled ? 1 : 0,
                    event.alternateSetting);
    }
    else if (event.type == EspUsbAudioEventType::SampleRateChanged)
    {
      rateEventCount++;
      Serial.printf("AUDIO_RATE %lu n=%lu\n",
                    static_cast<unsigned long>(event.sampleRate),
                    static_cast<unsigned long>(rateEventCount));
    }
    else if (event.type == EspUsbAudioEventType::VolumeChanged)
    {
      volumeEventCount++;
      Serial.printf("DEV_VOL ch=%u db=%d n=%lu\n",
                    event.channel, event.volumeDb256,
                    static_cast<unsigned long>(volumeEventCount));
    }
    else if (event.type == EspUsbAudioEventType::MuteChanged)
    {
      muteEventCount++;
      Serial.printf("DEV_MUTE ch=%u m=%u n=%lu\n",
                    event.channel, event.muted ? 1 : 0,
                    static_cast<unsigned long>(muteEventCount));
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
    if (command == 'r')
    {
      rxBytes = 0;
      rxReported = false;
      micTxBytes = 0;
      playback.resetStats();
      capture.resetStats();
      Serial.println("UAC2_RESET");
    }
    else if (command == '?')
    {
      const EspUsbAudioStreamStats playbackStats = playback.stats();
      const EspUsbAudioStreamStats captureStats = capture.stats();
      Serial.printf(
          "UAC2_ALIVE rx=%lu tx=%lu usb_rx=%lu usb_tx=%lu "
          "play_overruns=%lu cap_underruns=%lu events=%lu\n",
          static_cast<unsigned long>(rxBytes),
          static_cast<unsigned long>(micTxBytes),
          static_cast<unsigned long>(playbackStats.transferredBytes),
          static_cast<unsigned long>(captureStats.transferredBytes),
          static_cast<unsigned long>(playbackStats.overrunCount),
          static_cast<unsigned long>(captureStats.underrunCount),
          static_cast<unsigned long>(audio.droppedEvents()));
    }
    else if (command == 's')
    {
      // The device's own view of the control state the host programmed.
      bool masterMute = false;
      bool channelMute = false;
      bool captureMute = false;
      int16_t masterVolume = 0;
      int16_t channelVolume = 0;
      int16_t captureVolume = 0;
      EspUsbAudioVolumeRange range;
      audio.getMute(masterMute, EspUsbAudioDirection::Playback, 0);
      audio.getMute(channelMute, EspUsbAudioDirection::Playback, 1);
      audio.getVolume(masterVolume, EspUsbAudioDirection::Playback, 0);
      audio.getVolume(channelVolume, EspUsbAudioDirection::Playback, 1);
      audio.getVolumeRange(range, EspUsbAudioDirection::Playback, 0);
      // The capture Feature Unit is reported too: the host addresses "the first
      // unit" when a sketch does not name one, and this is what makes it visible
      // which unit that resolved to.
      audio.getMute(captureMute, EspUsbAudioDirection::Capture, 0);
      audio.getVolume(captureVolume, EspUsbAudioDirection::Capture, 0);
      Serial.printf(
          "UAC2_STATE proto=%s rate=%lu master_mute=%u master_vol=%d "
          "ch1_mute=%u ch1_vol=%d cap_mute=%u cap_vol=%d range=%d:%d:%d\n",
          audio.protocol() == EspUsbAudioProtocol::Uac2 ? "uac2" : "uac1",
          static_cast<unsigned long>(audio.currentSampleRate()),
          masterMute ? 1 : 0,
          masterVolume,
          channelMute ? 1 : 0,
          channelVolume,
          captureMute ? 1 : 0,
          captureVolume,
          range.min, range.max, range.resolution);
    }
  }
  delay(1);
}
