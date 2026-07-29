#include "EspUsbDevice.h"
#include "tusb.h"

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

static void testDefaultUac1()
{
  EspUsbDevice device;
  EspUsbAudioFunction audio(device);
  auto &playback = audio.addPlaybackStream();
  check(audio.protocol() == EspUsbAudioProtocol::Uac1,
        "uac1_default_protocol");
  check(playback.addFormat({48000, 2, 2, 16}), "uac1_format");

  EspUsbDeviceConfig config;
  config.startTinyUsb = false;
  check(device.begin(config), "uac1_begin");

  const uint8_t *descriptor = device.configurationDescriptor(0);
  const uint16_t length = read16(descriptor + 2);
  bool foundHeader = false;
  bool foundFormat = false;
  bool foundFeatureControls = false;
  uint8_t featureUnitId = 0;
  bool foundDataEndpoint = false;
  bool foundFeedbackEndpoint = false;
  for (uint16_t offset = 9;
       offset + 2 <= length && descriptor[offset] >= 2;
       offset = static_cast<uint16_t>(offset + descriptor[offset]))
  {
    const uint8_t descriptorLength = descriptor[offset];
    const uint8_t descriptorType = descriptor[offset + 1];
    if (offset + descriptorLength > length)
    {
      break;
    }
    if (descriptorType == 0x24 && descriptorLength >= 5 &&
        descriptor[offset + 2] == 0x01 &&
        read16(descriptor + offset + 3) == 0x0100)
    {
      foundHeader = true;
    }
    if (descriptorType == 0x24 && descriptorLength == 11 &&
        descriptor[offset + 2] == 0x02 &&
        descriptor[offset + 3] == 0x01)
    {
      const uint32_t rate =
          static_cast<uint32_t>(descriptor[offset + 8]) |
          (static_cast<uint32_t>(descriptor[offset + 9]) << 8) |
          (static_cast<uint32_t>(descriptor[offset + 10]) << 16);
      foundFormat = descriptor[offset + 4] == 2 &&
                    descriptor[offset + 5] == 2 &&
                    descriptor[offset + 6] == 16 &&
                    descriptor[offset + 7] == 1 && rate == 48000;
    }
    if (descriptorType == 0x24 && descriptorLength == 10 &&
        descriptor[offset + 2] == 0x06)
    {
      foundFeatureControls = descriptor[offset + 5] == 1 &&
                             descriptor[offset + 6] == 0x03 &&
                             descriptor[offset + 7] == 0x03 &&
                             descriptor[offset + 8] == 0x03;
      featureUnitId = descriptor[offset + 3];
    }
    if (descriptorType == 0x05 && descriptorLength == 9)
    {
      const uint8_t address = descriptor[offset + 2];
      if ((address & 0x80) == 0)
      {
        foundDataEndpoint = descriptor[offset + 3] == 0x09;
      }
      else
      {
        foundFeedbackEndpoint = true;
      }
    }
  }
  check(foundHeader, "uac1_header");
  check(foundFormat, "uac1_type_i_format");
  check(foundFeatureControls, "uac1_feature_channel_controls");
  check(foundDataEndpoint, "uac1_adaptive_data_endpoint");
  check(!foundFeedbackEndpoint, "uac1_no_feedback_endpoint");

  check(audio.hasMute(EspUsbAudioDirection::Playback, 0) &&
            audio.hasMute(EspUsbAudioDirection::Playback, 1) &&
            audio.hasMute(EspUsbAudioDirection::Playback, 2) &&
            !audio.hasMute(EspUsbAudioDirection::Playback, 3),
        "uac1_mute_capabilities");
  check(audio.hasVolume(EspUsbAudioDirection::Playback, 0) &&
            audio.hasVolume(EspUsbAudioDirection::Playback, 1) &&
            audio.hasVolume(EspUsbAudioDirection::Playback, 2) &&
            !audio.hasVolume(EspUsbAudioDirection::Capture, 0),
        "uac1_volume_capabilities");
  bool muted = false;
  int16_t volume = 0;
  EspUsbAudioVolumeRange range;
  check(audio.setMute(true, EspUsbAudioDirection::Playback, 1) &&
            audio.getMute(muted, EspUsbAudioDirection::Playback, 1) &&
            muted,
        "uac1_set_get_left_mute");
  check(audio.setVolume(-12 * 256, EspUsbAudioDirection::Playback, 2) &&
            audio.getVolume(volume, EspUsbAudioDirection::Playback, 2) &&
            volume == -12 * 256,
        "uac1_set_get_right_volume");
  check(audio.getVolumeRange(range, EspUsbAudioDirection::Playback, 2) &&
            range.min == -90 * 256 && range.max == 0 &&
            range.resolution == 256,
        "uac1_volume_range");
  check(!audio.setVolume(-385, EspUsbAudioDirection::Playback, 0),
        "uac1_volume_resolution_rejected");
  EspUsbAudioEvent event;
  check(audio.pollEvent(event) &&
            event.type == EspUsbAudioEventType::MuteChanged &&
            event.channel == 1 && event.muted,
        "uac1_local_mute_event");
  check(audio.pollEvent(event) &&
            event.type == EspUsbAudioEventType::VolumeChanged &&
            event.channel == 2 && event.volumeDb256 == -12 * 256,
        "uac1_local_volume_event");

  tusb_control_request_t request = {};
  request.bRequest = 0x01;
  request.wIndex = static_cast<uint16_t>(featureUnitId) << 8;
  request.wValue = static_cast<uint16_t>((1U << 8) | 2U);
  request.wLength = 1;
  const uint8_t muteRight[] = {1};
  check(featureUnitId != 0 &&
            audio.handleSetEntityRequest(&request, muteRight) &&
            audio.getMute(muted, EspUsbAudioDirection::Playback, 2) &&
            muted,
        "uac1_host_set_right_mute");
  request.wValue = static_cast<uint16_t>((2U << 8) | 1U);
  request.wLength = 2;
  const uint8_t minusSixDb[] = {0x00, 0xfa};
  check(audio.handleSetEntityRequest(&request, minusSixDb) &&
            audio.getVolume(volume, EspUsbAudioDirection::Playback, 1) &&
            volume == -6 * 256,
        "uac1_host_set_left_volume");
  check(audio.pollEvent(event) &&
            event.type == EspUsbAudioEventType::MuteChanged &&
            event.channel == 2 && event.muted,
        "uac1_host_mute_event");
  check(audio.pollEvent(event) &&
            event.type == EspUsbAudioEventType::VolumeChanged &&
            event.channel == 1 && event.volumeDb256 == -6 * 256,
        "uac1_host_volume_event");
  device.end();
}

static void testPlayback()
{
  EspUsbDevice device;
  EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2);
  auto &playback = audio.addPlaybackStream();
  check(playback.addFormat({48000, 2, 2, 16}), "playback_format");

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
  EspUsbAudioEvent event;
  check(!audio.pollEvent(event) && audio.pendingEvents() == 0 &&
            audio.droppedEvents() == 0,
        "playback_event_queue_empty");

  tusb_control_request_t request = {};
  request.bRequest = 0x01;
  request.wIndex = static_cast<uint16_t>(3U << 8);
  request.wValue = static_cast<uint16_t>(1U << 8);
  request.wLength = 1;
  const uint8_t mute[] = {1};
  check(audio.handleSetEntityRequest(&request, mute),
        "playback_mute_request");

  request.wValue = static_cast<uint16_t>(2U << 8);
  request.wLength = 2;
  const uint8_t minusSixDb[] = {0x00, 0xfa};
  check(audio.handleSetEntityRequest(&request, minusSixDb),
        "playback_volume_request");

  request.wIndex = 1;
  request.wValue = 1;
  request.wLength = 0;
  check(audio.handleSetInterface(&request),
        "playback_interface_request");
  check(audio.pendingEvents() == 3, "playback_events_pending");
  check(audio.pollEvent(event) &&
            event.type == EspUsbAudioEventType::MuteChanged &&
            event.target == EspUsbAudioEventTarget::Playback &&
            event.channel == 0 && event.muted,
        "playback_mute_event");
  check(audio.pollEvent(event) &&
            event.type == EspUsbAudioEventType::VolumeChanged &&
            event.target == EspUsbAudioEventTarget::Playback &&
            event.volumeDb256 == -6 * 256,
        "playback_volume_event");
  check(audio.pollEvent(event) &&
            event.type == EspUsbAudioEventType::StreamStateChanged &&
            event.target == EspUsbAudioEventTarget::Playback &&
            event.enabled && event.alternateSetting == 1,
        "playback_stream_event");
  check(!audio.pollEvent(event), "playback_events_drained");
  check(playback.stats().transferredBytes == 0 &&
            playback.stats().overrunCount == 0,
        "playback_stats_empty");
  check(audio.handlePlaybackTransfer(196, 1) &&
            playback.stats().transferredBytes == 196,
        "playback_stats_transfer");
  playback.resetStats();
  check(playback.stats().transferredBytes == 0,
        "playback_stats_reset");
  check(!playback.clearBuffer(), "playback_buffer_not_started");
  audio.clearEvents();
  device.end();
}

static bool findUac1Format(const uint8_t *descriptor, uint8_t channels,
                           uint8_t bytesPerSample, uint8_t bitsPerSample,
                           uint16_t &packetSize)
{
  const uint16_t length = read16(descriptor + 2);
  bool formatFound = false;
  packetSize = 0;
  for (uint16_t offset = 9;
       offset + 2 <= length && descriptor[offset] >= 2;
       offset = static_cast<uint16_t>(offset + descriptor[offset]))
  {
    const uint8_t descriptorLength = descriptor[offset];
    const uint8_t descriptorType = descriptor[offset + 1];
    if (offset + descriptorLength > length)
    {
      break;
    }
    if (descriptorType == 0x24 && descriptorLength == 11 &&
        descriptor[offset + 2] == 0x02 &&
        descriptor[offset + 3] == 0x01)
    {
      formatFound = descriptor[offset + 4] == channels &&
                    descriptor[offset + 5] == bytesPerSample &&
                    descriptor[offset + 6] == bitsPerSample;
    }
    if (descriptorType == 0x05 && descriptorLength == 9 &&
        (descriptor[offset + 2] & 0x80) == 0)
    {
      packetSize = read16(descriptor + offset + 4);
    }
  }
  return formatFound;
}

static void testUac1HighResolutionFormats()
{
  {
    EspUsbDevice device;
    EspUsbAudioFunction audio(device);
    auto &playback = audio.addPlaybackStream();
    check(playback.addFormat({48000, 2, 3, 24}), "uac1_24bit_format");
    EspUsbDeviceConfig config;
    config.startTinyUsb = false;
    check(device.begin(config), "uac1_24bit_begin");
    uint16_t packetSize = 0;
    check(findUac1Format(device.configurationDescriptor(0), 2, 3, 24,
                         packetSize) &&
              packetSize == 294,
          "uac1_24bit_descriptor");
    check(audio.handlePlaybackTransfer(packetSize, 1) &&
              playback.stats().transferredBytes == packetSize,
          "uac1_24bit_transfer_accounting");
    device.end();
  }

  {
    EspUsbDevice device;
    EspUsbAudioFunction audio(device);
    auto &playback = audio.addPlaybackStream();
    check(playback.addFormat({96000, 2, 4, 32}), "uac1_32bit_format");
    EspUsbDeviceConfig config;
    config.startTinyUsb = false;
    check(device.begin(config), "uac1_32bit_begin");
    uint16_t packetSize = 0;
    check(findUac1Format(device.configurationDescriptor(0), 2, 4, 32,
                         packetSize) &&
              packetSize == 776,
          "uac1_32bit_descriptor");
    check(audio.handlePlaybackTransfer(packetSize, 1) &&
              playback.stats().transferredBytes == packetSize,
          "uac1_32bit_transfer_accounting");
    device.end();
  }
}

static void testCapture()
{
  EspUsbDevice device;
  EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2);
  auto &capture = audio.addCaptureStream();
  check(capture.addFormat({48000, 1, 2, 16}), "capture_format");

  EspUsbDeviceConfig config;
  config.startTinyUsb = false;
  check(device.begin(config), "capture_begin");
  const uint8_t *full = device.configurationDescriptor(0);
  check(read16(full + 2) == 141 && full[4] == 2,
        "capture_configuration");
  check(read16(full + 130) == 98, "capture_fs_packet");
  check(capture.stats().transferredBytes == 0 &&
            capture.stats().underrunCount == 0,
        "capture_stats_empty");
  check(audio.handleCaptureTransfer(0, 0),
        "capture_inactive_transfer_ignored");
  capture.resetStats();
  check(!capture.clearBuffer(), "capture_buffer_not_started");
  device.end();
}

static void testDuplex()
{
  EspUsbDevice device;
  EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2);
  auto &playback = audio.addPlaybackStream();
  auto &capture = audio.addCaptureStream();
  check(playback.addFormat({48000, 2, 2, 16}) &&
            capture.addFormat({48000, 1, 2, 16}),
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
  testDefaultUac1();
  testPlayback();
  testUac1HighResolutionFormats();
  testCapture();
  testDuplex();
  Serial.printf("TEST_END pass=%d fail=%d\n", passCount, failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
  Serial.flush();
}

void loop()
{
}
