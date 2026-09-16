#include "EspUsbDevice.h"

#include <string.h>

#if __has_include("soc/soc_caps.h")
#include "soc/soc_caps.h"
#endif

#if defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED
#include "tusb.h"
#include "class/video/video_device.h"
#define ESP_USB_VIDEO_HAS_TINYUSB 1
#else
#define ESP_USB_VIDEO_HAS_TINYUSB 0
#endif

// USB Video Class camera.
//
// The descriptor is written here rather than through the TUD_VIDEO_DESC_*
// macros because those take their frame size, rate and bit rate as macro
// arguments, and this class decides them at begin() from what the sketch set.
// The bytes are the same; tests/single/video_descriptor pins them against a
// macro-built reference so the two cannot drift.

namespace
{

EspUsbDeviceVideo *g_activeVideo = nullptr;

constexpr uint8_t DESC_CS_INTERFACE = 0x24;
constexpr uint8_t DESC_INTERFACE = 0x04;
constexpr uint8_t DESC_INTERFACE_ASSOCIATION = 0x0b;
constexpr uint8_t DESC_ENDPOINT = 0x05;
constexpr uint8_t CLASS_VIDEO = 0x0e;
constexpr uint8_t SUBCLASS_CONTROL = 0x01;
constexpr uint8_t SUBCLASS_STREAMING = 0x02;
constexpr uint8_t SUBCLASS_INTERFACE_COLLECTION = 0x03;
constexpr uint8_t ITF_PROTOCOL_15 = 0x01;
constexpr uint8_t ITF_PROTOCOL_UNDEFINED = 0x00;

constexpr uint8_t VC_HEADER = 0x01;
constexpr uint8_t VC_INPUT_TERMINAL = 0x02;
constexpr uint8_t VC_OUTPUT_TERMINAL = 0x03;
constexpr uint8_t VS_INPUT_HEADER = 0x01;
constexpr uint8_t VS_FORMAT_UNCOMPRESSED = 0x04;
constexpr uint8_t VS_FRAME_UNCOMPRESSED = 0x05;
constexpr uint8_t VS_FORMAT_MJPEG = 0x06;
constexpr uint8_t VS_FRAME_MJPEG = 0x07;
constexpr uint8_t VS_COLORFORMAT = 0x0d;

constexpr uint16_t BCD_UVC_1_50 = 0x0150;
constexpr uint16_t TT_STREAMING = 0x0101;
constexpr uint16_t ITT_CAMERA = 0x0201;
// UVC's clock is only a unit for the timestamps in payload headers; nothing
// here emits them, and hosts accept any non-zero value. 27 MHz is what the
// specification's own examples use.
constexpr uint32_t CLOCK_FREQUENCY = 27000000u;

// Entity IDs inside this function. Two entities: the camera feeds the USB
// streaming terminal, which is what bTerminalLink points at.
constexpr uint8_t TERMINAL_CAMERA = 1;
constexpr uint8_t TERMINAL_OUTPUT = 2;

constexpr uint16_t MAX_DIMENSION = 4096;

// YUY2, as a Windows-style format GUID. The first four bytes are the FourCC
// and the rest is the fixed USB/DirectShow suffix.
const uint8_t GUID_YUY2[16] = {
    0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

struct Writer
{
  uint8_t *dst;
  size_t capacity;
  size_t offset = 0;
  bool overflowed = false;

  void u8(uint8_t value)
  {
    if (offset >= capacity)
    {
      overflowed = true;
      return;
    }
    dst[offset++] = value;
  }
  void u16(uint16_t value)
  {
    u8(static_cast<uint8_t>(value & 0xff));
    u8(static_cast<uint8_t>(value >> 8));
  }
  void u32(uint32_t value)
  {
    u16(static_cast<uint16_t>(value & 0xffff));
    u16(static_cast<uint16_t>(value >> 16));
  }
  void bytes(const uint8_t *data, size_t length)
  {
    for (size_t i = 0; i < length; i++)
    {
      u8(data[i]);
    }
  }
};

} // namespace

#if ESP_USB_VIDEO_HAS_TINYUSB
extern "C" void tud_video_frame_xfer_complete_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx)
{
  (void)ctl_idx;
  (void)stm_idx;
  if (g_activeVideo)
  {
    g_activeVideo->handleFrameComplete();
  }
}

extern "C" int tud_video_commit_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx,
                                   video_probe_and_commit_control_t const *parameters)
{
  (void)ctl_idx;
  (void)stm_idx;
  if (g_activeVideo && parameters)
  {
    EspUsbDeviceVideoCommit commit;
    commit.formatIndex = parameters->bFormatIndex;
    commit.frameIndex = parameters->bFrameIndex;
    commit.frameInterval = parameters->dwFrameInterval;
    commit.maxVideoFrameSize = parameters->dwMaxVideoFrameSize;
    commit.maxPayloadTransferSize = parameters->dwMaxPayloadTransferSize;
    g_activeVideo->handleCommit(commit);
  }
  // VIDEO_ERROR_NONE. Accepting whatever the host committed to is right for a
  // device that advertises one format at one size: the host can only have
  // chosen it, and the frame interval it asks for is a pacing hint this device
  // is free to miss.
  return 0;
}
#endif

EspUsbDeviceVideo::EspUsbDeviceVideo(EspUsbDevice &device, const char *name)
    : EspUsbDeviceClass(device), name_(name)
{
}

EspUsbDeviceVideo::~EspUsbDeviceVideo()
{
  end();
}

void EspUsbDeviceVideo::assignFunctionIds(uint8_t instance, uint8_t stringIndex)
{
  instance_ = instance;
  stringIndex_ = stringIndex;
}

bool EspUsbDeviceVideo::setFormat(EspUsbDeviceVideoFormat format)
{
  if (running_)
  {
    return false;
  }
  format_ = format;
  return true;
}

bool EspUsbDeviceVideo::setFrameSize(uint16_t width, uint16_t height)
{
  // Both dimensions must be even for YUY2: it packs two pixels into four
  // bytes, so an odd width has no whole macropixel at the end of a line.
  if (running_ || width == 0 || height == 0 || width > MAX_DIMENSION ||
      height > MAX_DIMENSION || (width & 1) != 0 || (height & 1) != 0)
  {
    return false;
  }
  width_ = width;
  height_ = height;
  return true;
}

bool EspUsbDeviceVideo::setFrameRate(uint8_t framesPerSecond)
{
  // 1..120. The frame interval is 10^7 / fps in 100 ns units, so 0 would
  // divide by zero and anything above 120 is beyond what the transport can
  // carry at any size worth sending.
  if (running_ || framesPerSecond == 0 || framesPerSecond > 120)
  {
    return false;
  }
  frameRate_ = framesPerSecond;
  return true;
}

bool EspUsbDeviceVideo::setMaxFrameSize(uint32_t bytes)
{
  if (running_)
  {
    return false;
  }
  maxFrameSize_ = bytes;
  return true;
}

uint32_t EspUsbDeviceVideo::maxFrameSize() const
{
  if (maxFrameSize_ != 0)
  {
    return maxFrameSize_;
  }
  const uint32_t uncompressed =
      static_cast<uint32_t>(width_) * static_cast<uint32_t>(height_) * 2u;
  // MJPEG's default is a compression ratio of 8, which is conservative for
  // photographic content and is only an advertised upper bound - a sketch
  // whose encoder produces larger frames must raise it with setMaxFrameSize().
  return format_ == EspUsbDeviceVideoFormat::Yuy2 ? uncompressed
                                                  : (uncompressed / 8u);
}

uint16_t EspUsbDeviceVideo::payloadBufferSize()
{
#if ESP_USB_VIDEO_HAS_TINYUSB
  return CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE;
#else
  return 0;
#endif
}

uint16_t EspUsbDeviceVideo::isochronousPacketSize(bool highSpeed)
{
  // Full speed reserves one packet per 1 ms frame and caps it at 1023 bytes;
  // high speed reserves one per 125 us microframe and caps it at 1024. The
  // per-transaction opportunities a high-bandwidth endpoint would add are not
  // used here: TinyUSB's video driver arms one transfer per interval.
  //
  // The endpoint must be at least as large as the payload the class driver
  // commits to, which is capped by CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE - so
  // that buffer is the floor here. Advertising less would leave the host
  // unable to open the streaming alternate setting.
  const uint16_t limit = highSpeed ? 1024 : 1023;
#if ESP_USB_VIDEO_HAS_TINYUSB
  const uint16_t staging = CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE;
  return staging > limit ? limit : (staging > 64 ? staging : limit);
#else
  return limit;
#endif
}

bool EspUsbDeviceVideo::bulkStreaming()
{
#if ESP_USB_VIDEO_HAS_TINYUSB && CFG_TUD_VIDEO_STREAMING_BULK
  return true;
#else
  return false;
#endif
}

uint16_t EspUsbDeviceVideo::descriptorLength(bool highSpeed) const
{
  (void)highSpeed;
  // IAD 8, standard VC 9, class VC header 12 + 1 collection entry,
  // camera terminal 18, output terminal 9, standard VS 9,
  // class VS input header 13 + 1 format entry, format, frame 38,
  // colour matching 6.
  uint16_t length = 8 + 9 + 13 + 18 + 9 + 9 + 14 + 38 + 6;
  length = static_cast<uint16_t>(
      length + (format_ == EspUsbDeviceVideoFormat::Yuy2 ? 27 : 11));
  // Isochronous streaming needs a second alternate setting to park the
  // bandwidth reservation on: alternate 0 declares no endpoint so an idle
  // camera reserves nothing. Bulk has no reservation to make, so its endpoint
  // sits in alternate 0 and there is no second setting.
  length = static_cast<uint16_t>(length + (bulkStreaming() ? 7 : 9 + 7));
  return length;
}

uint16_t EspUsbDeviceVideo::configurationDescriptorForSpeed(
    uint8_t *dst, size_t capacity, uint8_t interfaceNumber,
    uint8_t endpointNumber, bool highSpeed)
{
  if (!dst || endpointNumber == 0 || endpointNumber > 15 ||
      descriptorLength(highSpeed) > capacity)
  {
    return 0;
  }

  const uint8_t vcInterface = interfaceNumber;
  const uint8_t vsInterface = static_cast<uint8_t>(interfaceNumber + 1);
  const uint8_t endpointAddress = static_cast<uint8_t>(0x80 | endpointNumber);
  const bool uncompressed = format_ == EspUsbDeviceVideoFormat::Yuy2;
  const bool bulk = bulkStreaming();
  const uint32_t frameBytes = maxFrameSize();
  // 100 ns units, which is how UVC counts frame intervals.
  const uint32_t frameInterval = 10000000u / frameRate_;
  const uint32_t bitRate = frameBytes * 8u * frameRate_;

  Writer w{dst, capacity};

  // Interface association: the two interfaces are one function, which is what
  // makes Windows load usbccgp and bind usbvideo.sys to the pair rather than
  // to each interface on its own.
  w.u8(8);
  w.u8(DESC_INTERFACE_ASSOCIATION);
  w.u8(vcInterface);
  w.u8(2);
  w.u8(CLASS_VIDEO);
  w.u8(SUBCLASS_INTERFACE_COLLECTION);
  w.u8(ITF_PROTOCOL_UNDEFINED);
  w.u8(stringIndex_);

  // Video control interface, no endpoints: the optional interrupt endpoint
  // only carries status events (button presses, control changes), none of
  // which this function has, and leaving it out saves an endpoint number.
  w.u8(9);
  w.u8(DESC_INTERFACE);
  w.u8(vcInterface);
  w.u8(0);
  w.u8(0);
  w.u8(CLASS_VIDEO);
  w.u8(SUBCLASS_CONTROL);
  w.u8(ITF_PROTOCOL_15);
  w.u8(stringIndex_);

  // Class-specific VC header. wTotalLength covers this descriptor and the two
  // terminals below, and TinyUSB reads it to find where the control interface
  // ends, so it has to be exact.
  w.u8(13);
  w.u8(DESC_CS_INTERFACE);
  w.u8(VC_HEADER);
  w.u16(BCD_UVC_1_50);
  w.u16(13 + 18 + 9);
  w.u32(CLOCK_FREQUENCY);
  w.u8(1);
  w.u8(vsInterface);

  // Camera input terminal. The focal length fields are zero, which says the
  // lens is fixed, and bmControls is zero: no zoom, focus or exposure control.
  w.u8(18);
  w.u8(DESC_CS_INTERFACE);
  w.u8(VC_INPUT_TERMINAL);
  w.u8(TERMINAL_CAMERA);
  w.u16(ITT_CAMERA);
  w.u8(0);
  w.u8(0);
  w.u16(0);
  w.u16(0);
  w.u16(0);
  w.u8(3);
  w.u8(0);
  w.u8(0);
  w.u8(0);

  // Output terminal: the camera's pixels leave over USB.
  w.u8(9);
  w.u8(DESC_CS_INTERFACE);
  w.u8(VC_OUTPUT_TERMINAL);
  w.u8(TERMINAL_OUTPUT);
  w.u16(TT_STREAMING);
  w.u8(0);
  w.u8(TERMINAL_CAMERA);
  w.u8(0);

  // Video streaming interface, alternate 0. Isochronous parks here with no
  // endpoint so an idle camera reserves no bandwidth; bulk carries its
  // endpoint here because there is nothing to reserve.
  w.u8(9);
  w.u8(DESC_INTERFACE);
  w.u8(vsInterface);
  w.u8(0);
  w.u8(bulk ? 1 : 0);
  w.u8(CLASS_VIDEO);
  w.u8(SUBCLASS_STREAMING);
  w.u8(ITF_PROTOCOL_15);
  w.u8(stringIndex_);

  // Class-specific VS input header. wTotalLength again covers this descriptor
  // through the colour matching one.
  const uint16_t formatLength = uncompressed ? 27 : 11;
  w.u8(14);
  w.u8(DESC_CS_INTERFACE);
  w.u8(VS_INPUT_HEADER);
  w.u8(1);
  w.u16(static_cast<uint16_t>(14 + formatLength + 38 + 6));
  w.u8(endpointAddress);
  // bmInfo: no dynamic format change.
  w.u8(0);
  w.u8(TERMINAL_OUTPUT);
  // No still capture, no hardware trigger.
  w.u8(0);
  w.u8(0);
  w.u8(0);
  w.u8(1);
  // bmaControls: nothing about the format is negotiable beyond probe/commit.
  w.u8(0);

  if (uncompressed)
  {
    w.u8(27);
    w.u8(DESC_CS_INTERFACE);
    w.u8(VS_FORMAT_UNCOMPRESSED);
    w.u8(1);
    w.u8(1);
    w.bytes(GUID_YUY2, sizeof(GUID_YUY2));
    w.u8(16);
    w.u8(1);
    // Aspect ratio fields apply to interlaced streams only; zero elsewhere.
    w.u8(0);
    w.u8(0);
    w.u8(0);
    w.u8(0);
  }
  else
  {
    w.u8(11);
    w.u8(DESC_CS_INTERFACE);
    w.u8(VS_FORMAT_MJPEG);
    w.u8(1);
    w.u8(1);
    // bmFlags bit 0: sample size is not fixed, which is what JPEG is.
    w.u8(0);
    w.u8(1);
    w.u8(0);
    w.u8(0);
    w.u8(0);
    w.u8(0);
  }

  // Frame descriptor, continuous form with min = max = step 0, which is how a
  // single fixed rate is expressed.
  w.u8(38);
  w.u8(DESC_CS_INTERFACE);
  w.u8(uncompressed ? VS_FRAME_UNCOMPRESSED : VS_FRAME_MJPEG);
  w.u8(1);
  // bmCapabilities: no still image, no fixed frame rate requirement.
  w.u8(0);
  w.u16(width_);
  w.u16(height_);
  w.u32(bitRate);
  w.u32(bitRate);
  w.u32(frameBytes);
  w.u32(frameInterval);
  w.u8(0);
  w.u32(frameInterval);
  w.u32(frameInterval);
  w.u32(0);

  // Colour matching: BT.709 primaries and transfer, BT.601 matrix, which is
  // what both MJPEG and YUY2 webcams overwhelmingly report.
  w.u8(6);
  w.u8(DESC_CS_INTERFACE);
  w.u8(VS_COLORFORMAT);
  w.u8(1);
  w.u8(1);
  w.u8(4);

  if (!bulk)
  {
    // Alternate 1 carries the isochronous endpoint. Selecting it is what makes
    // the host reserve the bandwidth, and dropping back to alternate 0 is what
    // releases it.
    w.u8(9);
    w.u8(DESC_INTERFACE);
    w.u8(vsInterface);
    w.u8(1);
    w.u8(1);
    w.u8(CLASS_VIDEO);
    w.u8(SUBCLASS_STREAMING);
    w.u8(ITF_PROTOCOL_15);
    w.u8(0);
  }

  w.u8(7);
  w.u8(DESC_ENDPOINT);
  w.u8(endpointAddress);
  if (bulk)
  {
    w.u8(0x02);
    w.u16(highSpeed ? 512 : 64);
    w.u8(0);
  }
  else
  {
    // Isochronous, asynchronous, data endpoint. bInterval 1 is every frame at
    // full speed and every microframe at high speed.
    w.u8(0x05);
    w.u16(isochronousPacketSize(highSpeed));
    w.u8(1);
  }

  if (w.overflowed || w.offset != descriptorLength(highSpeed))
  {
    return 0;
  }
  return static_cast<uint16_t>(w.offset);
}

uint16_t EspUsbDeviceVideo::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber,
                                                    uint8_t endpointNumber, uint16_t endpointSize)
{
  // endpointSize is the caller's full-speed/high-speed hint for bulk classes.
  // Video derives its own packet size from the transfer type, so the hint only
  // selects the speed.
  return configurationDescriptorForSpeed(dst, descriptorLength(endpointSize > 64),
                                         interfaceNumber, endpointNumber,
                                         endpointSize > 64);
}

bool EspUsbDeviceVideo::begin()
{
#if ESP_USB_VIDEO_HAS_TINYUSB
  // One camera per device: TinyUSB's video driver is built with CFG_TUD_VIDEO
  // control interfaces, and the callbacks above have one destination.
  if (g_activeVideo && g_activeVideo != this)
  {
    return false;
  }
  g_activeVideo = this;
#endif
  inFlight_.store(false);
  commit_ = EspUsbDeviceVideoCommit{};
  running_ = true;
  return true;
}

void EspUsbDeviceVideo::end()
{
  running_ = false;
  inFlight_.store(false);
#if ESP_USB_VIDEO_HAS_TINYUSB
  if (g_activeVideo == this)
  {
    g_activeVideo = nullptr;
  }
#endif
}

bool EspUsbDeviceVideo::streaming() const
{
#if ESP_USB_VIDEO_HAS_TINYUSB
  return running_ && tud_video_n_streaming(instance_, 0);
#else
  return false;
#endif
}

bool EspUsbDeviceVideo::sendFrame(const void *frame, size_t length)
{
#if ESP_USB_VIDEO_HAS_TINYUSB
  if (!frame || length == 0 || !streaming())
  {
    return false;
  }
  // The class driver walks the buffer with a 32-bit offset and the host was
  // told an upper bound; a frame beyond either is a sketch bug worth failing
  // rather than truncating.
  if (length > maxFrameSize())
  {
    return false;
  }
  // Take the slot before touching the driver. Two tasks reaching here at once
  // - a completion callback arming the next frame while the sketch's loop
  // arms one too - would otherwise both call into TinyUSB, whose frame
  // pointer, size and offset are one unguarded structure.
  bool expected = false;
  if (!inFlight_.compare_exchange_strong(expected, true))
  {
    return false;
  }
  // const_cast: TinyUSB's signature is non-const because a bufferless sketch
  // may hand it a scratch buffer, but this path only reads from it.
  if (!tud_video_n_frame_xfer(instance_, 0, const_cast<void *>(frame), length))
  {
    inFlight_.store(false);
    return false;
  }
  return true;
#else
  (void)frame;
  (void)length;
  return false;
#endif
}

void EspUsbDeviceVideo::onFrameComplete(FrameCompleteCallback callback)
{
  frameCompleteCallback_ = callback;
}

void EspUsbDeviceVideo::onCommit(CommitCallback callback)
{
  commitCallback_ = callback;
}

EspUsbDeviceVideoCommit EspUsbDeviceVideo::commit() const
{
  return commit_;
}

void EspUsbDeviceVideo::handleFrameComplete()
{
  inFlight_.store(false);
  if (frameCompleteCallback_)
  {
    frameCompleteCallback_();
  }
}

void EspUsbDeviceVideo::handleCommit(const EspUsbDeviceVideoCommit &commit)
{
  commit_ = commit;
  if (commitCallback_)
  {
    commitCallback_(commit);
  }
}
