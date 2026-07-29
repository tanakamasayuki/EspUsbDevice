#include "EspUsbDevice.h"

static int passCount = 0;
static int failCount = 0;

static void check(bool condition, const char *name)
{
  if (condition)
  {
    ++passCount;
  }
  else
  {
    Serial.print("FAIL ");
    Serial.println(name);
    ++failCount;
  }
}

static uint16_t read16(const uint8_t *data)
{
  return static_cast<uint16_t>(data[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

static void testPlayback()
{
  EspUsbDevice device;
  EspUsbAudioFunction audio(device);
  auto &playback = audio.addPlaybackStream();
  check(playback.channels(2), "playback_channels");
  check(playback.addFormat({48000, 2, 16}), "playback_format");

  EspUsbDeviceConfig config;
  config.startTinyUsb = false;
  check(device.begin(config), "playback_begin");

  const uint8_t *full = device.configurationDescriptorForSpeed(0, false);
  const uint8_t *high = device.configurationDescriptorForSpeed(0, true);
  check(read16(full + 2) == 152 && full[4] == 2,
        "playback_configuration");
  check(full[9] == 8 && full[10] == 0x0b && full[15] == 0x20,
        "playback_iad");
  check(read16(full + 134) == 196 && read16(full + 149) == 4,
        "playback_fs_packets");
  check(read16(high + 134) == 28 && read16(high + 149) == 4,
        "playback_hs_packets");
  device.end();
}

static void testCapture()
{
  EspUsbDevice device;
  EspUsbAudioFunction audio(device);
  auto &capture = audio.addCaptureStream();
  check(capture.channels(1), "capture_channels");
  check(capture.addFormat({48000, 2, 16}), "capture_format");

  EspUsbDeviceConfig config;
  config.startTinyUsb = false;
  check(device.begin(config), "capture_begin");
  const uint8_t *full = device.configurationDescriptor(0);
  check(read16(full + 2) == 141 && full[4] == 2,
        "capture_configuration");
  check(read16(full + 130) == 98, "capture_fs_packet");
  device.end();
}

static void testDuplex()
{
  EspUsbDevice device;
  EspUsbAudioFunction audio(device);
  auto &playback = audio.addPlaybackStream();
  auto &capture = audio.addCaptureStream();
  check(playback.channels(2) &&
            playback.addFormat({48000, 2, 16}) &&
            capture.channels(1) &&
            capture.addFormat({48000, 2, 16}),
        "duplex_streams");

  EspUsbDeviceConfig config;
  config.startTinyUsb = false;
  check(device.begin(config), "duplex_begin");
  const uint8_t *full = device.configurationDescriptor(0);
  check(full[4] == 3 && full[12] == 3,
        "duplex_interfaces");
  check(read16(full + 2) <= 256, "duplex_fits_configuration");
  device.end();
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  Serial.println("TEST_BEGIN audio_v2_descriptor");
  testPlayback();
  testCapture();
  testDuplex();
  Serial.printf("TEST_END pass=%d fail=%d\n", passCount, failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
  Serial.flush();
}

void loop()
{
}
