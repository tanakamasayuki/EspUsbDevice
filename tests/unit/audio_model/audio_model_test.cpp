#include "internal/EspUsbAudioModel.h"
#include "internal/EspUsbAudioDescriptor.h"
#include "internal/EspUsbAudioControl.h"
#include "internal/EspUsbAudioEvent.h"
#include "internal/EspUsbAudioRequest.h"

#include <iostream>

using namespace espusb::internal;

static int failures = 0;

static void check(bool condition, const char *name)
{
  if (!condition)
  {
    std::cerr << "FAIL " << name << '\n';
    ++failures;
  }
}

static uint16_t read16(const uint8_t *data)
{
  return static_cast<uint16_t>(data[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

static uint32_t read32(const uint8_t *data)
{
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

int main()
{
  AudioPacketRequirement packet;
  const AudioPcmFormat stereo16{48000, 2, 2, 16};
  check(validateAudioFormat(stereo16, UsbSpeed::Full,
                            defaultAudioBusLimits(UsbSpeed::Full), &packet) ==
            AudioFormatError::None,
        "stereo16_fs_valid");
  check(packet.bytesPerSecond == 192000 && packet.maxPacketSize == 196 &&
            packet.framesPerSecond == 1000,
        "stereo16_fs_packet");

  check(validateAudioFormat(stereo16, UsbSpeed::High,
                            defaultAudioBusLimits(UsbSpeed::High), &packet) ==
            AudioFormatError::None,
        "stereo16_hs_valid");
  check(packet.maxPacketSize == 28 && packet.framesPerSecond == 8000,
        "stereo16_hs_packet");

  const AudioPcmFormat stereo32_96k{96000, 2, 4, 32};
  check(validateAudioFormat(stereo32_96k, UsbSpeed::Full,
                            defaultAudioBusLimits(UsbSpeed::Full), &packet) ==
            AudioFormatError::None &&
            packet.maxPacketSize == 776,
        "stereo32_96k_fs");

  const AudioPcmFormat stereo32_192k{192000, 2, 4, 32};
  check(validateAudioFormat(stereo32_192k, UsbSpeed::Full,
                            defaultAudioBusLimits(UsbSpeed::Full), &packet) ==
            AudioFormatError::PacketTooLarge,
        "stereo32_192k_fs_rejected");
  check(validateAudioFormat(stereo32_192k, UsbSpeed::High,
                            defaultAudioBusLimits(UsbSpeed::High), &packet) ==
            AudioFormatError::None &&
            packet.maxPacketSize == 200,
        "stereo32_192k_hs");

  check(validateAudioFormat({48000, 3, 2, 16}, UsbSpeed::Full,
                            defaultAudioBusLimits(UsbSpeed::Full)) ==
            AudioFormatError::InvalidChannelCount,
        "channel_limit");
  check(validateAudioFormat({48000, 2, 2, 24}, UsbSpeed::Full,
                            defaultAudioBusLimits(UsbSpeed::Full)) ==
            AudioFormatError::InvalidBitResolution,
        "valid_bits_fit_subslot");

  AudioBusLimits smallBuffer = defaultAudioBusLimits(UsbSpeed::Full);
  smallBuffer.softwareBufferSize = 128;
  check(validateAudioFormat(stereo16, UsbSpeed::Full, smallBuffer) ==
            AudioFormatError::BufferTooSmall,
        "software_buffer_limit");

  AudioFunctionModel function(AudioProtocol::Uac2);
  check(function.protocol() == AudioProtocol::Uac2,
        "protocol_independent_of_speed");
  check(function.playback().addFormat(
            stereo16, UsbSpeed::Full,
            defaultAudioBusLimits(UsbSpeed::Full)),
        "playback_add");
  check(!function.playback().addFormat(
            stereo16, UsbSpeed::Full,
            defaultAudioBusLimits(UsbSpeed::Full)) &&
            function.playback().error() == AudioFormatError::DuplicateFormat,
        "duplicate_rejected");
  check(function.capture().formatCount() == 0,
        "capture_independent");

  DescriptorLayout layout(EndpointLimits{5, 5, 5});
  AudioFunctionGraph graph;
  check(buildAudioFunctionGraph(function, layout, AudioFunctionGraphConfig{},
                                graph),
        "playback_graph");
  check(graph.controlInterface == 0 && graph.playback.interfaceNumber == 1 &&
            layout.interfaceCount() == 2,
        "playback_interfaces");
  check(graph.clockSourceId == 1 && graph.entityCount == 4 &&
            graph.playback.terminalLink == 2,
        "playback_entities");
  check(graph.playback.dataEndpoint == 0x01 &&
            graph.playback.feedbackEndpoint == 0x81,
        "playback_endpoints");
  check(graph.entities[2].kind == AudioEntityKind::FeatureUnit &&
            graph.entities[2].sourceId == 2 &&
            graph.entities[3].sourceId == 3,
        "playback_signal_chain");

  AudioFunctionModel duplex;
  check(duplex.playback().addFormat(
            stereo16, UsbSpeed::Full,
            defaultAudioBusLimits(UsbSpeed::Full)) &&
            duplex.capture().addFormat(
                {48000, 1, 2, 16}, UsbSpeed::Full,
                defaultAudioBusLimits(UsbSpeed::Full)),
        "duplex_formats");
  DescriptorLayout duplexLayout(EndpointLimits{5, 5, 5});
  AudioFunctionGraph duplexGraph;
  check(buildAudioFunctionGraph(duplex, duplexLayout,
                                AudioFunctionGraphConfig{}, duplexGraph),
        "duplex_graph");
  check(duplexGraph.entityCount == 7 &&
            duplexGraph.capture.interfaceNumber == 2 &&
            duplexGraph.capture.dataEndpoint == 0x82 &&
            duplexGraph.capture.terminalLink == 7,
        "duplex_layout");

  AudioFunctionModel empty;
  DescriptorLayout emptyLayout(EndpointLimits{5, 5, 5});
  AudioFunctionGraph emptyGraph;
  check(!buildAudioFunctionGraph(empty, emptyLayout,
                                 AudioFunctionGraphConfig{}, emptyGraph) &&
            emptyGraph.error == AudioGraphError::NoStreams,
        "empty_graph_rejected");

  AudioFunctionModel inconsistent;
  check(inconsistent.playback().addFormat(
            {48000, 1, 2, 16}, UsbSpeed::Full,
            defaultAudioBusLimits(UsbSpeed::Full)) &&
            inconsistent.playback().addFormat(
                stereo16, UsbSpeed::Full,
                defaultAudioBusLimits(UsbSpeed::Full)),
        "inconsistent_formats_added");
  DescriptorLayout inconsistentLayout(EndpointLimits{5, 5, 5});
  AudioFunctionGraph inconsistentGraph;
  check(!buildAudioFunctionGraph(inconsistent, inconsistentLayout,
                                 AudioFunctionGraphConfig{},
                                 inconsistentGraph) &&
            inconsistentGraph.error == AudioGraphError::InconsistentChannels,
        "inconsistent_topology_rejected");

  AudioFunctionModel incompatibleRates;
  check(incompatibleRates.playback().addFormat(
            stereo16, UsbSpeed::Full,
            defaultAudioBusLimits(UsbSpeed::Full)) &&
            incompatibleRates.capture().addFormat(
                {96000, 2, 2, 16}, UsbSpeed::Full,
                defaultAudioBusLimits(UsbSpeed::Full)),
        "incompatible_clock_formats_added");
  DescriptorLayout incompatibleLayout(EndpointLimits{5, 5, 5});
  AudioFunctionGraph incompatibleGraph;
  check(!buildAudioFunctionGraph(incompatibleRates, incompatibleLayout,
                                 AudioFunctionGraphConfig{},
                                 incompatibleGraph) &&
            incompatibleGraph.error ==
                AudioGraphError::IncompatibleClockRates,
        "shared_clock_rates_rejected");

  DescriptorLayout constrained(EndpointLimits{1, 1, 1});
  AudioFunctionGraph constrainedGraph;
  check(!buildAudioFunctionGraph(duplex, constrained,
                                 AudioFunctionGraphConfig{},
                                 constrainedGraph) &&
            constrainedGraph.error == AudioGraphError::LayoutAllocation,
        "endpoint_limit_rejected");

  uint8_t playbackDescriptor[256] = {};
  DescriptorBuffer playbackBuffer(playbackDescriptor,
                                  sizeof(playbackDescriptor));
  DescriptorBuildContext playbackContext(UsbSpeed::Full, playbackBuffer);
  AudioDescriptorError descriptorError = AudioDescriptorError::InvalidGraph;
  check(playbackContext.beginConfiguration(layout.interfaceCount(), 1, 0x80,
                                           50) &&
            writeUac2Function(playbackContext, function, graph,
                              AudioDescriptorConfig{}, &descriptorError) &&
            playbackContext.endConfiguration(),
        "playback_uac2_descriptor");
  check(descriptorError == AudioDescriptorError::None &&
            playbackBuffer.size() == 152 &&
            read16(playbackDescriptor + 2) == 152,
        "playback_uac2_total_length");
  check(playbackDescriptor[9] == 8 && playbackDescriptor[10] == 0x0b &&
            playbackDescriptor[11] == 0 &&
            playbackDescriptor[12] == 2 &&
            playbackDescriptor[15] == 0x20,
        "playback_uac2_iad");
  check(playbackDescriptor[26] == 9 &&
            playbackDescriptor[28] == 0x01 &&
            read16(playbackDescriptor + 32) == 64,
        "playback_uac2_ac_header");
  check(playbackDescriptor[35] == 8 &&
            playbackDescriptor[37] == 0x0a &&
            playbackDescriptor[38] == graph.clockSourceId,
        "playback_uac2_clock");
  check(playbackDescriptor[60] == 18 &&
            playbackDescriptor[62] == 0x06 &&
            read32(playbackDescriptor + 65) == 0x0f &&
            read32(playbackDescriptor + 69) == 0x0f &&
            read32(playbackDescriptor + 73) == 0x0f,
        "playback_uac2_feature_channels");
  check(playbackDescriptor[90 + 3] == 0 &&
            playbackDescriptor[99 + 3] == 1 &&
            playbackDescriptor[99 + 4] == 2,
        "playback_uac2_alternates");
  check(playbackDescriptor[124] == 6 &&
            playbackDescriptor[128] == 2 &&
            playbackDescriptor[129] == 16,
        "playback_uac2_format");
  check(playbackDescriptor[132] == graph.playback.dataEndpoint &&
            read16(playbackDescriptor + 134) == 196 &&
            playbackDescriptor[147] == graph.playback.feedbackEndpoint &&
            read16(playbackDescriptor + 149) == 4,
        "playback_uac2_fs_endpoints");

  uint8_t highDescriptor[256] = {};
  DescriptorBuffer highBuffer(highDescriptor, sizeof(highDescriptor));
  DescriptorBuildContext highContext(UsbSpeed::High, highBuffer);
  check(highContext.beginConfiguration(layout.interfaceCount(), 1, 0x80, 50) &&
            writeUac2Function(highContext, function, graph,
                              AudioDescriptorConfig{}) &&
            highContext.endConfiguration(),
        "playback_uac2_hs_descriptor");
  check(read16(highDescriptor + 134) == 28 &&
            read16(highDescriptor + 149) == 4 &&
            highDescriptor[151] == 4,
        "playback_uac2_hs_endpoints");

  AudioFunctionModel captureOnly;
  check(captureOnly.capture().addFormat(
            {48000, 1, 2, 16}, UsbSpeed::Full,
            defaultAudioBusLimits(UsbSpeed::Full)),
        "capture_only_format");
  DescriptorLayout captureLayout(EndpointLimits{5, 5, 5});
  AudioFunctionGraph captureGraph;
  check(buildAudioFunctionGraph(captureOnly, captureLayout,
                                AudioFunctionGraphConfig{}, captureGraph),
        "capture_only_graph");
  uint8_t captureDescriptor[256] = {};
  DescriptorBuffer captureBuffer(captureDescriptor,
                                 sizeof(captureDescriptor));
  DescriptorBuildContext captureContext(UsbSpeed::Full, captureBuffer);
  check(captureContext.beginConfiguration(captureLayout.interfaceCount(), 1,
                                          0x80, 50) &&
            writeUac2Function(captureContext, captureOnly, captureGraph,
                              AudioDescriptorConfig{}) &&
            captureContext.endConfiguration(),
        "capture_uac2_descriptor");
  check(captureBuffer.size() == 141 &&
            captureDescriptor[9 + 6] == 0x20 &&
            captureDescriptor[95 + 4] == 1 &&
            captureDescriptor[128] == captureGraph.capture.dataEndpoint &&
            read16(captureDescriptor + 130) == 98,
        "capture_uac2_layout");

  uint8_t duplexDescriptor[256] = {};
  DescriptorBuffer duplexBuffer(duplexDescriptor, sizeof(duplexDescriptor));
  DescriptorBuildContext duplexContext(UsbSpeed::Full, duplexBuffer);
  check(duplexContext.beginConfiguration(duplexLayout.interfaceCount(), 1,
                                         0x80, 50) &&
            writeUac2Function(duplexContext, duplex, duplexGraph,
                              AudioDescriptorConfig{}) &&
            duplexContext.endConfiguration(),
        "duplex_uac2_descriptor");
  check(duplexDescriptor[4] == 3 && duplexDescriptor[12] == 3 &&
            duplexBuffer.size() <= sizeof(duplexDescriptor),
        "duplex_uac2_interfaces");

  AudioFunctionModel multipleFormats;
  check(multipleFormats.playback().addFormat(
            stereo16, UsbSpeed::Full,
            defaultAudioBusLimits(UsbSpeed::Full)) &&
            multipleFormats.playback().addFormat(
                {96000, 2, 2, 16}, UsbSpeed::Full,
                defaultAudioBusLimits(UsbSpeed::Full)),
        "multiple_formats_model");
  DescriptorLayout multipleLayout(EndpointLimits{5, 5, 5});
  AudioFunctionGraph multipleGraph;
  check(buildAudioFunctionGraph(multipleFormats, multipleLayout,
                                AudioFunctionGraphConfig{}, multipleGraph),
        "multiple_formats_graph");
  uint8_t multipleDescriptor[256] = {};
  DescriptorBuffer multipleBuffer(multipleDescriptor,
                                  sizeof(multipleDescriptor));
  DescriptorBuildContext multipleContext(UsbSpeed::Full, multipleBuffer);
  check(multipleContext.beginConfiguration(multipleLayout.interfaceCount(), 1,
                                           0x80, 50) &&
            !writeUac2Function(multipleContext, multipleFormats,
                               multipleGraph, AudioDescriptorConfig{},
                               &descriptorError) &&
            descriptorError == AudioDescriptorError::UnsupportedFormatCount,
        "multiple_formats_explicitly_deferred");

  AudioControlState controls;
  check(controls.configure(duplex, duplexGraph) &&
            controls.sampleRateCount() == 1 &&
            controls.sampleRate(0) == 48000,
        "control_state_configure");
  int32_t current = -1;
  check(controls.current(duplexGraph.clockSourceId,
                         AudioControlSelector::SampleRate, 0, current) &&
            current == 48000,
        "clock_current_rate");
  check(controls.current(duplexGraph.clockSourceId,
                         AudioControlSelector::ClockValid, 0, current) &&
            current == 1,
        "clock_valid");
  check(!controls.setCurrent(duplexGraph.clockSourceId,
                             AudioControlSelector::ClockValid, 0, 0) &&
            controls.error() == AudioControlError::ReadOnly,
        "clock_valid_read_only");
  check(!controls.setCurrent(duplexGraph.clockSourceId,
                             AudioControlSelector::SampleRate, 0, 44100) &&
            controls.error() == AudioControlError::InvalidValue,
        "unsupported_rate_rejected");

  const uint8_t playbackFeature = duplexGraph.entities[2].id;
  const uint8_t captureFeature = duplexGraph.entities[5].id;
  check(controls.setCurrent(playbackFeature, AudioControlSelector::Mute,
                            0, 1) &&
            controls.current(playbackFeature, AudioControlSelector::Mute,
                             0, current) &&
            current == 1,
        "playback_mute_state");
  check(controls.setCurrent(captureFeature, AudioControlSelector::Volume,
                            0, -6 * 256) &&
            controls.current(captureFeature, AudioControlSelector::Volume,
                             0, current) &&
            current == -6 * 256,
        "capture_volume_state");
  AudioControlRange volumeRange;
  check(controls.range(captureFeature, AudioControlSelector::Volume, 0,
                       volumeRange) &&
            volumeRange.minimum == -90 * 256 &&
            volumeRange.maximum == 0 &&
            volumeRange.resolution == 256,
        "volume_range");
  check(!controls.setCurrent(captureFeature, AudioControlSelector::Volume,
                             0, -385) &&
            controls.error() == AudioControlError::InvalidValue,
        "volume_resolution_rejected");
  check(controls.setCurrent(playbackFeature, AudioControlSelector::Mute,
                            1, 1) &&
            controls.current(playbackFeature, AudioControlSelector::Mute,
                             1, current) &&
            current == 1,
        "playback_left_mute_state");
  check(controls.setCurrent(playbackFeature, AudioControlSelector::Volume,
                            2, -12 * 256) &&
            controls.current(playbackFeature, AudioControlSelector::Volume,
                             2, current) &&
            current == -12 * 256,
        "playback_right_volume_state");
  check(controls.current(captureFeature, AudioControlSelector::Mute,
                         1, current),
        "capture_mono_channel_control");
  check(!controls.current(captureFeature, AudioControlSelector::Mute,
                          2, current) &&
            controls.error() == AudioControlError::InvalidChannel,
        "capture_invalid_channel_rejected");
  check(!controls.current(playbackFeature, AudioControlSelector::Mute,
                          3, current) &&
            controls.error() == AudioControlError::InvalidChannel,
        "playback_invalid_channel_rejected");
  check(!controls.current(99, AudioControlSelector::Mute, 0, current) &&
            controls.error() == AudioControlError::UnknownEntity,
        "unknown_entity_rejected");

  uint8_t responseStorage[128] = {};
  DescriptorBuffer response(responseStorage, sizeof(responseStorage));
  Uac2EntityRequest request;
  request.request = 0x01;
  request.selector = 0x01;
  request.interfaceNumber = duplexGraph.controlInterface;
  request.entityId = duplexGraph.clockSourceId;
  request.length = 4;
  AudioRequestError requestError = AudioRequestError::UnknownEntity;
  check(writeUac2EntityResponse(controls, duplexGraph.controlInterface,
                                request, response, &requestError) &&
            requestError == AudioRequestError::None &&
            response.size() == 4 &&
            responseStorage[0] == 0x80 &&
            responseStorage[1] == 0xbb,
        "uac2_clock_cur_response");

  response.reset();
  request.request = 0x02;
  request.length = 14;
  check(writeUac2EntityResponse(controls, duplexGraph.controlInterface,
                                request, response) &&
            response.size() == 14 &&
            read16(responseStorage) == 1 &&
            responseStorage[2] == 0x80 &&
            responseStorage[6] == 0x80,
        "uac2_clock_range_response");

  response.reset();
  request.entityId = captureFeature;
  request.selector = 0x02;
  request.length = 8;
  check(writeUac2EntityResponse(controls, duplexGraph.controlInterface,
                                request, response) &&
            response.size() == 8 &&
            read16(responseStorage) == 1 &&
            read16(responseStorage + 2) == 0xa600 &&
            read16(responseStorage + 6) == 0x0100,
        "uac2_volume_range_response");

  request.request = 0x01;
  request.length = 2;
  const uint8_t minusTwelveDb[] = {0x00, 0xf4};
  AudioControlChange change;
  check(applyUac2EntityRequest(controls, duplexGraph.controlInterface,
                               request, minusTwelveDb,
                               sizeof(minusTwelveDb), nullptr, &change) &&
            change.changed &&
            change.selector == AudioControlSelector::Volume &&
            change.entityId == captureFeature &&
            change.value == -12 * 256 &&
            controls.current(captureFeature, AudioControlSelector::Volume,
                             0, current) &&
            current == -12 * 256,
        "uac2_volume_set_change");

  check(applyUac2EntityRequest(controls, duplexGraph.controlInterface,
                               request, minusTwelveDb,
                               sizeof(minusTwelveDb), nullptr, &change) &&
            !change.changed,
        "uac2_unchanged_control_not_event");

  request.entityId = playbackFeature;
  request.selector = 0x01;
  request.length = 1;
  const uint8_t unmute[] = {0};
  check(applyUac2EntityRequest(controls, duplexGraph.controlInterface,
                               request, unmute, sizeof(unmute)) &&
            controls.current(playbackFeature, AudioControlSelector::Mute,
                             0, current) &&
            current == 0,
        "uac2_mute_set_request");

  request.interfaceNumber =
      static_cast<uint8_t>(duplexGraph.controlInterface + 1);
  check(!applyUac2EntityRequest(controls, duplexGraph.controlInterface,
                                request, unmute, sizeof(unmute),
                                &requestError) &&
            requestError == AudioRequestError::InvalidInterface,
        "uac2_wrong_interface_rejected");
  request.interfaceNumber = duplexGraph.controlInterface;
  request.length = 2;
  check(!applyUac2EntityRequest(controls, duplexGraph.controlInterface,
                                request, unmute, sizeof(unmute),
                                &requestError) &&
            requestError == AudioRequestError::InvalidLength,
        "uac2_wrong_length_rejected");

  AudioEventQueue queue;
  for (uint8_t i = 0; i < AudioEventQueue::CAPACITY; ++i)
  {
    AudioRuntimeEvent event;
    event.type = AudioRuntimeEventType::Volume;
    event.target = AudioRuntimeEventTarget::Capture;
    event.channel = i;
    event.value = -static_cast<int32_t>(i) * 256;
    check(queue.push(event), "event_queue_fill");
  }
  AudioRuntimeEvent overflow;
  check(!queue.push(overflow) &&
            queue.pending() == AudioEventQueue::CAPACITY &&
            queue.dropped() == 1,
        "event_queue_overflow_counted");
  for (uint8_t i = 0; i < AudioEventQueue::CAPACITY; ++i)
  {
    AudioRuntimeEvent event;
    check(queue.pop(event) &&
              event.type == AudioRuntimeEventType::Volume &&
              event.target == AudioRuntimeEventTarget::Capture &&
              event.channel == i &&
              event.value == -static_cast<int32_t>(i) * 256,
          "event_queue_fifo");
  }
  check(!queue.pop(overflow) && queue.pending() == 0,
        "event_queue_empty");

  AudioRuntimeEvent streamEvent;
  streamEvent.type = AudioRuntimeEventType::StreamState;
  streamEvent.target = AudioRuntimeEventTarget::Playback;
  streamEvent.alternateSetting = 1;
  streamEvent.value = 1;
  check(queue.push(streamEvent) && queue.pop(overflow) &&
            overflow.type == AudioRuntimeEventType::StreamState &&
            overflow.alternateSetting == 1,
        "event_queue_wrap");
  queue.clear();
  check(queue.pending() == 0 && queue.dropped() == 0,
        "event_queue_clear");

  if (failures)
  {
    return 1;
  }
  std::cout << "Audio model checks passed\n";
  return 0;
}
