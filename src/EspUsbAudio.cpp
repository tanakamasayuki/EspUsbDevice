#include "EspUsbDevice.h"
#include "internal/EspUsbAudioRequest.h"

#include <string.h>

#if __has_include("soc/soc_caps.h")
#include "soc/soc_caps.h"
#endif

#if defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED
#include "tusb.h"
#include "class/audio/audio_device.h"
#define ESP_USB_AUDIO_HAS_TINYUSB 1
#else
#define ESP_USB_AUDIO_HAS_TINYUSB 0
#endif

namespace {

EspUsbAudioFunction *g_activeAudio = nullptr;

#if ESP_USB_AUDIO_HAS_TINYUSB
espusb::internal::Uac2EntityRequest
toEntityRequest(const tusb_control_request_t *request)
{
  espusb::internal::Uac2EntityRequest result;
  result.request = request->bRequest;
  result.channel = static_cast<uint8_t>(request->wValue & 0xff);
  result.selector = static_cast<uint8_t>(request->wValue >> 8);
  result.interfaceNumber = static_cast<uint8_t>(request->wIndex & 0xff);
  result.entityId = static_cast<uint8_t>(request->wIndex >> 8);
  result.length = request->wLength;
  return result;
}
#endif

} // namespace

EspUsbAudioFunction::EspUsbAudioFunction(EspUsbDevice &device)
    : EspUsbDeviceClass(device),
      model_(espusb::internal::AudioProtocol::Uac2)
{
}

EspUsbAudioFunction::~EspUsbAudioFunction()
{
  end();
}

bool EspUsbAudioFunction::protocol(EspUsbAudioProtocol protocol)
{
  if (playbackEnabled_ || captureEnabled_)
  {
    return false;
  }
  protocol_ = protocol;
  model_ = espusb::internal::AudioFunctionModel(
      protocol == EspUsbAudioProtocol::Uac2
          ? espusb::internal::AudioProtocol::Uac2
          : espusb::internal::AudioProtocol::Uac1);
  return true;
}

EspUsbAudioPlaybackStream &EspUsbAudioFunction::addPlaybackStream()
{
  playbackEnabled_ = true;
  return playback_;
}

EspUsbAudioCaptureStream &EspUsbAudioFunction::addCaptureStream()
{
  captureEnabled_ = true;
  return capture_;
}

bool EspUsbAudioPlaybackStream::channels(uint8_t count)
{
  if (function_.model_.playback().formatCount() != 0 ||
      count == 0 || count > 2)
  {
    return false;
  }
  channels_ = count;
  return true;
}

bool EspUsbAudioPlaybackStream::addFormat(const EspUsbAudioFormat &format)
{
  return function_.addFormat(espusb::internal::AudioDirection::Playback,
                             channels_, format);
}

bool EspUsbAudioCaptureStream::channels(uint8_t count)
{
  if (function_.model_.capture().formatCount() != 0 ||
      count == 0 || count > 2)
  {
    return false;
  }
  channels_ = count;
  return true;
}

bool EspUsbAudioCaptureStream::addFormat(const EspUsbAudioFormat &format)
{
  return function_.addFormat(espusb::internal::AudioDirection::Capture,
                             channels_, format);
}

bool EspUsbAudioFunction::addFormat(
    espusb::internal::AudioDirection direction, uint8_t channels,
    const EspUsbAudioFormat &format)
{
  if (format.sampleRate == 0)
  {
    return false;
  }
  const espusb::internal::AudioPcmFormat internalFormat{
      format.sampleRate, channels, format.subslotBytes, format.validBits};
  espusb::internal::AudioStreamModel &stream =
      direction == espusb::internal::AudioDirection::Playback
          ? model_.playback()
          : model_.capture();
  // The initial UAC2 implementation emits one topology at both speeds.
  // Reject a format that cannot be represented on a negotiated FS link.
  return stream.addFormat(
      internalFormat, espusb::internal::UsbSpeed::Full,
      espusb::internal::defaultAudioBusLimits(
          espusb::internal::UsbSpeed::Full));
}

bool EspUsbAudioFunction::buildGraph(uint8_t interfaceNumber,
                                     uint8_t endpointNumber)
{
  if (interfaceNumber == 0xff || endpointNumber == 0 ||
      endpointNumber > 15)
  {
    return false;
  }
  espusb::internal::DescriptorLayout layout(
      espusb::internal::EndpointLimits{15, 15, 15});
  if (interfaceNumber != 0)
  {
    layout.allocateInterfaces(interfaceNumber);
  }
  for (uint8_t endpoint = 1; endpoint < endpointNumber; ++endpoint)
  {
    if (!layout.reserveEndpoint(endpoint) ||
        !layout.reserveEndpoint(static_cast<uint8_t>(0x80 | endpoint)))
    {
      return false;
    }
  }
  espusb::internal::AudioFunctionGraphConfig config;
  if (!espusb::internal::buildAudioFunctionGraph(model_, layout, config,
                                                  graph_))
  {
    return false;
  }
  return controls_.configure(model_, graph_);
}

bool EspUsbAudioFunction::begin()
{
  if (protocol_ != EspUsbAudioProtocol::Uac2 ||
      (!playbackEnabled_ && !captureEnabled_) ||
      (playbackEnabled_ && model_.playback().formatCount() != 1) ||
      (captureEnabled_ && model_.capture().formatCount() != 1))
  {
    return false;
  }
  if (g_activeAudio && g_activeAudio != this)
  {
    return false;
  }
  if (!buildGraph(graph_.controlInterface,
                  graph_.playback.dataEndpoint
                      ? static_cast<uint8_t>(
                            graph_.playback.dataEndpoint & 0x0f)
                      : 1))
  {
    return false;
  }
  g_activeAudio = this;
  return true;
}

void EspUsbAudioFunction::end()
{
  if (g_activeAudio == this)
  {
    g_activeAudio = nullptr;
  }
}

uint8_t EspUsbAudioFunction::interfaceCount() const
{
  return static_cast<uint8_t>(
      1U + (playbackEnabled_ ? 1U : 0U) + (captureEnabled_ ? 1U : 0U));
}

uint8_t EspUsbAudioFunction::endpointCount() const
{
  return static_cast<uint8_t>((playbackEnabled_ ? 1U : 0U) +
                              (captureEnabled_ ? 1U : 0U));
}

uint16_t EspUsbAudioFunction::configurationDescriptor(
    uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber,
    uint16_t endpointSize)
{
  return configurationDescriptorForSpeed(
      dst, 256, interfaceNumber, endpointNumber, endpointSize > 64);
}

uint16_t EspUsbAudioFunction::configurationDescriptorForSpeed(
    uint8_t *dst, size_t capacity, uint8_t interfaceNumber,
    uint8_t endpointNumber, bool highSpeed)
{
  if (!dst || !buildGraph(interfaceNumber, endpointNumber))
  {
    return 0;
  }
  espusb::internal::DescriptorBuffer buffer(dst, capacity);
  espusb::internal::DescriptorBuildContext context(
      highSpeed ? espusb::internal::UsbSpeed::High
                : espusb::internal::UsbSpeed::Full,
      buffer);
  espusb::internal::AudioDescriptorError error;
  if (!espusb::internal::writeUac2Function(
          context, model_, graph_, espusb::internal::AudioDescriptorConfig{},
          &error))
  {
    return 0;
  }
  return static_cast<uint16_t>(buffer.size());
}

int EspUsbAudioPlaybackStream::available() const
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  return static_cast<int>(tud_audio_available());
#else
  return 0;
#endif
}

size_t EspUsbAudioPlaybackStream::read(void *data, size_t length)
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  if (!data || length == 0)
  {
    return 0;
  }
  return tud_audio_read(data, static_cast<uint16_t>(
                                  length > 0xffffU ? 0xffffU : length));
#else
  (void)data;
  (void)length;
  return 0;
#endif
}

size_t EspUsbAudioCaptureStream::write(const void *data, size_t length)
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  if (!data || length == 0)
  {
    return 0;
  }
  return tud_audio_write(data, static_cast<uint16_t>(
                                   length > 0xffffU ? 0xffffU : length));
#else
  (void)data;
  (void)length;
  return 0;
#endif
}

uint32_t EspUsbAudioFunction::currentSampleRate() const
{
  return controls_.currentSampleRate();
}

bool EspUsbAudioFunction::handleGetEntityRequest(uint8_t rhport,
                                                 const void *rawRequest)
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  const auto *request =
      static_cast<const tusb_control_request_t *>(rawRequest);
  uint8_t responseData[CFG_TUD_AUDIO_CTRL_BUF_SZ] = {};
  espusb::internal::DescriptorBuffer response(responseData,
                                              sizeof(responseData));
  if (!espusb::internal::writeUac2EntityResponse(
          controls_, graph_.controlInterface, toEntityRequest(request),
          response))
  {
    return false;
  }
  return tud_audio_buffer_and_schedule_control_xfer(
      rhport, request, response.data(),
      static_cast<uint16_t>(response.size()));
#else
  (void)rhport;
  (void)rawRequest;
  return false;
#endif
}

bool EspUsbAudioFunction::handleSetEntityRequest(const void *rawRequest,
                                                 const uint8_t *data)
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  const auto *request =
      static_cast<const tusb_control_request_t *>(rawRequest);
  return espusb::internal::applyUac2EntityRequest(
      controls_, graph_.controlInterface, toEntityRequest(request), data,
      request->wLength);
#else
  (void)rawRequest;
  (void)data;
  return false;
#endif
}

#if ESP_USB_AUDIO_HAS_TINYUSB
extern "C" bool tud_audio_get_req_entity_cb(
    uint8_t rhport, const tusb_control_request_t *request)
{
  return g_activeAudio &&
         g_activeAudio->handleGetEntityRequest(rhport, request);
}

extern "C" bool tud_audio_set_req_entity_cb(
    uint8_t, const tusb_control_request_t *request, uint8_t *data)
{
  return g_activeAudio &&
         g_activeAudio->handleSetEntityRequest(request, data);
}

extern "C" bool tud_audio_get_req_ep_cb(
    uint8_t, const tusb_control_request_t *)
{
  return false;
}

extern "C" bool tud_audio_get_req_itf_cb(
    uint8_t, const tusb_control_request_t *)
{
  return false;
}

extern "C" bool tud_audio_set_req_ep_cb(
    uint8_t, const tusb_control_request_t *, uint8_t *)
{
  return false;
}

extern "C" bool tud_audio_set_req_itf_cb(
    uint8_t, const tusb_control_request_t *, uint8_t *)
{
  return false;
}

extern "C" bool tud_audio_set_itf_cb(
    uint8_t, const tusb_control_request_t *)
{
  return g_activeAudio != nullptr;
}

extern "C" bool tud_audio_set_itf_close_ep_cb(
    uint8_t, const tusb_control_request_t *)
{
  return g_activeAudio != nullptr;
}

extern "C" bool tud_audio_rx_done_isr(
    uint8_t, uint16_t, uint8_t, uint8_t, uint8_t)
{
  return g_activeAudio != nullptr;
}

extern "C" bool tud_audio_tx_done_isr(
    uint8_t, uint16_t, uint8_t, uint8_t, uint8_t)
{
  return g_activeAudio != nullptr;
}

extern "C" void tud_audio_feedback_params_cb(
    uint8_t, uint8_t, audio_feedback_params_t *params)
{
  if (!params || !g_activeAudio)
  {
    return;
  }
  params->method = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
  params->sample_freq = g_activeAudio->currentSampleRate();
}
#endif
