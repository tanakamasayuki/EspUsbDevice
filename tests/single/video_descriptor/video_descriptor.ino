#include "EspUsbDevice.h"

// Host-independent check of the UVC descriptor EspUsbDeviceVideo builds.
//
// The bytes are written by hand in EspUsbVideo.cpp because the frame size,
// rate and bit rate are runtime values, so this test is what keeps them
// honest: every length field is compared against what the descriptors that
// follow actually occupy, and the whole function is walked the way a host
// parses it. tests/manual/windows_uvc is what says a real host accepts it.

static int passCount = 0;
static int failCount = 0;

static void check(bool condition, const char *name)
{
  if (condition)
  {
    passCount++;
  }
  else
  {
    Serial.print("FAIL ");
    Serial.println(name);
    failCount++;
  }
}

static uint16_t le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p)
{
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Find the first descriptor of this type (and subtype, when type is 0x24)
// inside the configuration, from `from` onwards.
static const uint8_t *find(const uint8_t *cfg, uint16_t total, uint16_t from,
                           uint8_t type, uint8_t subtype)
{
  for (uint16_t o = from; o + 2 <= total && cfg[o]; o = static_cast<uint16_t>(o + cfg[o]))
  {
    if (cfg[o + 1] != type)
    {
      continue;
    }
    if (type != 0x24 || cfg[o + 2] == subtype)
    {
      return &cfg[o];
    }
  }
  return nullptr;
}

// Plain arguments rather than a struct: the Arduino preprocessor puts its
// generated prototypes above every definition in the sketch, so a parameter
// type declared here would not yet be visible where it inserts them.
static void testFunction(const char *name, EspUsbDeviceVideoFormat format,
                         uint16_t width, uint16_t height, uint8_t frameRate,
                         uint16_t formatLength, uint8_t frameSubtype)
{
  struct
  {
    const char *name;
    EspUsbDeviceVideoFormat format;
    uint16_t width;
    uint16_t height;
    uint8_t frameRate;
    uint16_t formatLength;
    uint8_t frameSubtype;
  } const e{name, format, width, height, frameRate, formatLength, frameSubtype};
  EspUsbDevice device;
  EspUsbDeviceVideo video(device, "Camera");
  check(video.setFormat(e.format), e.name);
  check(video.setFrameSize(e.width, e.height), e.name);
  check(video.setFrameRate(e.frameRate), e.name);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4091;
  config.startTinyUsb = false;
  check(device.begin(config), e.name);
  // Setters are refused once the descriptor has been built and published.
  check(!video.setFrameSize(640, 480), e.name);

  const uint8_t *cfg = device.configurationDescriptor(0);
  check(cfg != nullptr, e.name);
  if (!cfg)
  {
    device.end();
    return;
  }
  const uint16_t total = le16(&cfg[2]);
  check(cfg[4] == 2, e.name); // two interfaces, no more
  // An IAD means the device descriptor must declare the IAD class triple, or
  // Windows binds one driver across both interfaces.
  const uint8_t *dev = device.deviceDescriptor();
  check(dev[4] == 0xef && dev[5] == 0x02 && dev[6] == 0x01, e.name);

  const uint8_t *iad = find(cfg, total, 9, 0x0b, 0);
  check(iad != nullptr && iad[0] == 8 && iad[3] == 2 && iad[4] == 0x0e &&
            iad[5] == 0x03 && iad[6] == 0x00,
        e.name);

  // Video control interface: class 0x0e, subclass 1, protocol 1, no endpoints.
  const uint8_t *vc = find(cfg, total, 9, 0x04, 0);
  check(vc != nullptr && vc[4] == 0 && vc[5] == 0x0e && vc[6] == 0x01 && vc[7] == 0x01,
        e.name);
  check(iad && vc && iad[2] == vc[2], e.name); // bFirstInterface names the VC

  // Class-specific VC header: wTotalLength must cover itself and both
  // terminals, which is how a host finds the end of the control interface.
  const uint8_t *csvc = find(cfg, total, 9, 0x24, 0x01);
  check(csvc != nullptr && csvc[0] == 13, e.name);
  const uint8_t *camera = find(cfg, total, 9, 0x24, 0x02);
  const uint8_t *output = find(cfg, total, 9, 0x24, 0x03);
  check(camera != nullptr && camera[0] == 18, e.name);
  check(output != nullptr && output[0] == 9, e.name);
  check(csvc && camera && output && le16(&csvc[3 + 2]) == csvc[0] + camera[0] + output[0],
        e.name);
  // The streaming interface named in the collection is the next one up, and
  // the output terminal is fed by the camera.
  check(csvc && vc && csvc[11] == 1 && csvc[12] == vc[2] + 1, e.name);
  check(camera && output && output[7] == camera[3], e.name);

  // Streaming interface, alternate 0.
  const uint16_t vcOffset = static_cast<uint16_t>(vc - cfg);
  const uint8_t *vs0 = find(cfg, total, static_cast<uint16_t>(vcOffset + vc[0]), 0x04, 0);
  check(vs0 != nullptr && vs0[3] == 0 && vs0[5] == 0x0e && vs0[6] == 0x02 && vs0[7] == 0x01,
        e.name);
  check(vs0 && vc && vs0[2] == vc[2] + 1, e.name);
  // Isochronous keeps alternate 0 endpoint-free so an idle camera reserves no
  // bandwidth; bulk has nothing to reserve and carries its endpoint here.
  check(vs0 && vs0[4] == (EspUsbDeviceVideo::bulkStreaming() ? 1 : 0), e.name);

  const uint8_t *csvs = find(cfg, total, 9, 0x24, 0x01 + 0); // VS input header
  // The VC header has the same subtype, so search after the streaming
  // interface instead.
  csvs = find(cfg, total, static_cast<uint16_t>(vs0 - cfg), 0x24, 0x01);
  check(csvs != nullptr && csvs[0] == 14 && csvs[3] == 1, e.name);

  const uint8_t *fmt = find(cfg, total, static_cast<uint16_t>(vs0 - cfg), 0x24,
                            e.format == EspUsbDeviceVideoFormat::Yuy2 ? 0x04 : 0x06);
  check(fmt != nullptr && fmt[0] == e.formatLength, e.name);
  check(fmt && fmt[3] == 1 && fmt[4] == 1, e.name); // index 1, one frame

  const uint8_t *frame = find(cfg, total, static_cast<uint16_t>(vs0 - cfg), 0x24,
                              e.frameSubtype);
  check(frame != nullptr && frame[0] == 38, e.name);
  const uint8_t *color = find(cfg, total, static_cast<uint16_t>(vs0 - cfg), 0x24, 0x0d);
  check(color != nullptr && color[0] == 6, e.name);
  check(csvs && fmt && frame && color &&
            le16(&csvs[4]) == csvs[0] + fmt[0] + frame[0] + color[0],
        e.name);

  // The frame descriptor carries what the sketch asked for.
  if (frame)
  {
    check(le16(&frame[5]) == e.width && le16(&frame[7]) == e.height, e.name);
    const uint32_t interval = 10000000u / e.frameRate;
    check(le32(&frame[21]) == interval, e.name);   // dwDefaultFrameInterval
    check(frame[25] == 0, e.name);                 // continuous
    check(le32(&frame[26]) == interval && le32(&frame[30]) == interval, e.name);
    check(le32(&frame[17]) == video.maxFrameSize(), e.name);
    // Bit rate follows from the advertised frame size and rate.
    check(le32(&frame[9]) == video.maxFrameSize() * 8u * e.frameRate, e.name);
  }

  // The endpoint the streaming header points at is the one that exists.
  const uint8_t *ep = find(cfg, total, static_cast<uint16_t>(vs0 - cfg), 0x05, 0);
  check(ep != nullptr && ep[0] == 7 && (ep[2] & 0x80) != 0, e.name);
  check(csvs && ep && csvs[6] == ep[2], e.name);
  if (ep)
  {
    if (EspUsbDeviceVideo::bulkStreaming())
    {
      check((ep[3] & 0x03) == 0x02, e.name);
    }
    else
    {
      // Isochronous, asynchronous, one packet per interval.
      check((ep[3] & 0x03) == 0x01, e.name);
      check(le16(&ep[4]) == EspUsbDeviceVideo::isochronousPacketSize(false), e.name);
      // The endpoint must be at least the payload staging buffer, or TinyUSB
      // refuses to open the streaming alternate setting, and it must fit the
      // controller's transmit FIFO, or it opens and transmits nothing.
      check(le16(&ep[4]) >= EspUsbDeviceVideo::payloadBufferSize(), e.name);
      check(ep[6] == 1, e.name);
      // Alternate 1 is what the endpoint belongs to.
      const uint8_t *vs1 = find(cfg, total, static_cast<uint16_t>(vs0 - cfg + vs0[0]), 0x04, 0);
      check(vs1 != nullptr && vs1[3] == 1 && vs1[4] == 1 && vs1[2] == vs0[2], e.name);
    }
  }

  // What descriptorLength() promises is what the function occupies: the
  // configuration total minus its 9-byte header, with nothing else present.
  check(total == 9 + video.descriptorLength(false), e.name);
  device.end();
}

// A camera next to another function still gets its own interface numbers and
// its own endpoint, and the host-visible numbering follows registration order.
static void testComposite()
{
  const char *name = "composite";
  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceVideo video(device, "Camera");
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4091;
  config.startTinyUsb = false;
  check(device.begin(config), name);
  const uint8_t *cfg = device.configurationDescriptor(0);
  check(cfg != nullptr && cfg[4] == 3, name); // vendor + VC + VS
  if (!cfg)
  {
    device.end();
    return;
  }
  const uint16_t total = le16(&cfg[2]);
  const uint8_t *iad = find(cfg, total, 9, 0x0b, 0);
  // Vendor is registered first, so the camera starts at interface 1.
  check(iad != nullptr && iad[2] == 1, name);
  // Every interface number appears once and they are contiguous from 0.
  uint8_t seen = 0;
  for (uint16_t o = 9; o + 2 <= total && cfg[o]; o = static_cast<uint16_t>(o + cfg[o]))
  {
    if (cfg[o + 1] == 0x04 && cfg[o + 3] == 0)
    {
      seen = static_cast<uint8_t>(seen | (1u << cfg[o + 2]));
    }
  }
  check(seen == 0x07, name);
  device.end();
}

// A camera the controller's transmit FIFO can back starts; one it cannot is
// refused by begin() rather than enumerated as a camera that sends nothing.
// The S2/S3 FIFO is 256 words shared by everything, so a second isochronous
// function is what pushes a default camera over it.
static void testFifoBudget()
{
  const char *name = "fifo_budget";
  {
    EspUsbDevice device;
    EspUsbDeviceVideo video(device);
    EspUsbDeviceConfig config;
    config.vid = 0x303a;
    config.pid = 0x4091;
    config.startTinyUsb = false;
    check(device.begin(config), name);
    check(device.lastError() == ESP_OK, name);
    device.end();
  }
  {
    // Audio capture is the library's other isochronous IN function. Together
    // with the camera they exceed the S3's FIFO, and begin() has to say so.
    EspUsbDevice device;
    EspUsbDeviceVideo video(device);
    EspUsbAudioFunction audio(device);
    auto &capture = audio.addCaptureStream();
    check(capture.addFormat({48000, 2, 2, 16}), name);
    EspUsbDeviceConfig config;
    config.vid = 0x303a;
    config.pid = 0x4091;
    config.startTinyUsb = false;
    const bool started = device.begin(config);
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
    check(!started, name);
    check(device.lastError() == ESP_ERR_NO_MEM, name);
#else
    // A P4 high-speed controller has four times the FIFO, so this fits there.
    (void)started;
#endif
    device.end();
  }
}

// Out-of-range configuration is refused rather than emitted.
static void testRejects()
{
  const char *name = "rejects";
  EspUsbDevice device;
  EspUsbDeviceVideo video(device);
  check(!video.setFrameSize(0, 240), name);
  check(!video.setFrameSize(320, 0), name);
  check(!video.setFrameSize(321, 240), name); // odd width has no YUY2 macropixel
  check(!video.setFrameSize(320, 241), name);
  check(!video.setFrameSize(4098, 240), name);
  check(!video.setFrameRate(0), name);
  check(!video.setFrameRate(121), name);
  check(video.setFrameSize(640, 480), name);
  check(video.setFrameRate(30), name);
  // Defaults: YUY2 is exact, MJPEG is an eighth.
  check(video.setFormat(EspUsbDeviceVideoFormat::Yuy2), name);
  check(video.maxFrameSize() == 640u * 480u * 2u, name);
  check(video.setFormat(EspUsbDeviceVideoFormat::Mjpeg), name);
  check(video.maxFrameSize() == 640u * 480u * 2u / 8u, name);
  check(video.setMaxFrameSize(12345), name);
  check(video.maxFrameSize() == 12345, name);
  // Nothing can be sent before the host has started streaming.
  check(!video.streaming(), name);
  static uint8_t frame[16];
  check(!video.sendFrame(frame, sizeof(frame)), name);
  check(!video.sendFrame(nullptr, 1), name);
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("TEST_BEGIN video_descriptor");
  Serial.printf("VIDEO bulk=%d isoFs=%u isoHs=%u staging=%u\n",
                EspUsbDeviceVideo::bulkStreaming() ? 1 : 0,
                (unsigned)EspUsbDeviceVideo::isochronousPacketSize(false),
                (unsigned)EspUsbDeviceVideo::isochronousPacketSize(true),
                (unsigned)EspUsbDeviceVideo::payloadBufferSize());

  testFunction("mjpeg_qvga", EspUsbDeviceVideoFormat::Mjpeg, 320, 240, 15, 11, 0x07);
  testFunction("yuy2_qqvga", EspUsbDeviceVideoFormat::Yuy2, 160, 120, 10, 27, 0x05);
  testFunction("mjpeg_vga", EspUsbDeviceVideoFormat::Mjpeg, 640, 480, 30, 11, 0x07);
  testComposite();
  testFifoBudget();
  testRejects();

  Serial.printf("PASS %d FAIL %d\n", passCount, failCount);
  Serial.println("TEST_END");
  Serial.println(failCount == 0 ? "OK" : "NG");
}

void loop()
{
  delay(1000);
}
