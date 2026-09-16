// Does a real host accept the UVC descriptor this library builds, bind its
// own camera driver with nothing to install, and receive frames?
//
// Automated tests cannot answer that. tests/single/video_descriptor proves the
// bytes are right; only a host can say whether it streams. This sketch is a
// camera with a test pattern - no image sensor involved - so the question
// stays "does the USB side work".
//
// Build variants, from build_opt.h:
//   (none)                MJPEG 320x240 at 15 fps, from frames.h
//   -DVAR_FORMAT=1        YUY2, generated a line at a time
//   -DVAR_WIDTH= -DVAR_HEIGHT= -DVAR_FPS=
//   -DVAR_PID=            a different identity, for a PC that has seen this one
//   -DCFG_TUD_VIDEO_STREAMING_BULK=1   bulk instead of isochronous
//     (library-wide: needs arduino-cli --clean, see build_opt.h notes)
#include "EspUsbDevice.h"
#include "frames.h"

#ifndef VAR_FORMAT
#define VAR_FORMAT 0 // 0 MJPEG, 1 YUY2
#endif
#ifndef VAR_PID
#define VAR_PID 0x4092
#endif
#ifndef VAR_FPS
#define VAR_FPS 15
#endif
#if VAR_FORMAT == 1
// Uncompressed is generated, so any even size works; the default is small
// because a full-speed isochronous endpoint carries about 1 MB/s and one
// 320x240 YUY2 frame is 150 KB.
#ifndef VAR_WIDTH
#define VAR_WIDTH 160
#endif
#ifndef VAR_HEIGHT
#define VAR_HEIGHT 120
#endif
#else
#ifndef VAR_WIDTH
#define VAR_WIDTH UVC_FRAME_WIDTH
#endif
#ifndef VAR_HEIGHT
#define VAR_HEIGHT UVC_FRAME_HEIGHT
#endif
#endif

EspUsbDevice device;
EspUsbDeviceVideo Camera(device, "EspUsbDevice Camera");

#if VAR_FORMAT == 1
// One frame, rebuilt in place between transfers. The class driver reads this
// buffer payload by payload while the transfer is in flight, so it must not be
// touched until the completion callback has run.
static uint8_t frameBuffer[VAR_WIDTH * VAR_HEIGHT * 2];
#endif

static volatile uint32_t framesSent = 0;
static volatile uint32_t framesFailed = 0;
static volatile uint32_t bytesSent = 0;
static uint32_t frameIndex = 0;
static uint32_t streamStartedMs = 0;
static uint32_t nextFrameDueMs = 0;
static bool wasStreaming = false;

#if VAR_FORMAT == 1
// YUY2 colour bars with a block that steps across the bottom once per frame,
// so a viewer can tell a live stream from a stuck one.
static void fillFrame(uint32_t index)
{
  static const uint8_t bars[8][2] = {
      {235, 128}, {210, 16}, {170, 166}, {145, 54},
      {106, 202}, {81, 90},  {41, 240},  {16, 128}};
  const uint16_t barWidth = VAR_WIDTH / 8;
  const uint16_t blockRow = (VAR_HEIGHT * 3) / 4;
  const uint16_t blockWidth = VAR_WIDTH / 16;
  const uint16_t blockX = static_cast<uint16_t>((index % 16) * blockWidth);
  for (uint16_t y = 0; y < VAR_HEIGHT; y++)
  {
    uint8_t *row = &frameBuffer[static_cast<size_t>(y) * VAR_WIDTH * 2];
    // YUY2 packs two pixels into four bytes: Y0 U Y1 V.
    for (uint16_t x = 0; x < VAR_WIDTH; x += 2)
    {
      uint8_t luma = bars[(x / barWidth) & 7][0];
      uint8_t chroma = bars[(x / barWidth) & 7][1];
      if (y >= blockRow && x >= blockX && x < blockX + blockWidth)
      {
        luma = 235;
        chroma = 128;
      }
      row[x * 2 + 0] = luma;
      row[x * 2 + 1] = chroma;
      row[x * 2 + 2] = luma;
      row[x * 2 + 3] = 128;
    }
  }
}
#endif

// The DWC2 transmit FIFO is the thing an isochronous endpoint can outgrow, and
// nothing reports it: an endpoint whose FIFO could not be allocated still
// enumerates. These are the S3's registers, read straight from the peripheral
// (base 0x60080000, offsets from the DWC2 databook) after the host has
// configured the device, because they hold nothing meaningful before that.
static void reportFifo(const char *when)
{
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
  volatile uint32_t *dwc2 = reinterpret_cast<volatile uint32_t *>(0x60080000UL);
  const uint32_t ghwcfg3 = dwc2[0x04c / 4];
  const uint32_t grxfsiz = dwc2[0x024 / 4];
  const uint32_t dieptxf0 = dwc2[0x028 / 4];
  const uint32_t dieptxf1 = dwc2[0x104 / 4];
  const uint32_t gdfifocfg = dwc2[0x05c / 4];
  Serial.printf("UVC fifo@%s depth=%lu rx=%lu ep0in=%lu@%lu ep1in=%lu@%lu epinfo=%lu\n",
                when, (unsigned long)(ghwcfg3 >> 16),
                (unsigned long)(grxfsiz & 0xffff),
                (unsigned long)(dieptxf0 >> 16), (unsigned long)(dieptxf0 & 0xffff),
                (unsigned long)(dieptxf1 >> 16), (unsigned long)(dieptxf1 & 0xffff),
                (unsigned long)(gdfifocfg >> 16));
#else
  (void)when;
#endif
}

static bool armNextFrame()
{
#if VAR_FORMAT == 1
  fillFrame(frameIndex);
  const uint8_t *data = frameBuffer;
  const size_t length = sizeof(frameBuffer);
#else
  const uint8_t *data = kFrames[frameIndex % UVC_FRAME_COUNT];
  const size_t length = kFrameSizes[frameIndex % UVC_FRAME_COUNT];
#endif
  frameIndex++;
  if (!Camera.sendFrame(data, length))
  {
    framesFailed++;
    return false;
  }
  bytesSent += length;
  return true;
}

void setup()
{
  Serial.begin(115200);
  delay(2000);

#if VAR_FORMAT == 1
  Camera.setFormat(EspUsbDeviceVideoFormat::Yuy2);
#else
  Camera.setFormat(EspUsbDeviceVideoFormat::Mjpeg);
  // The generated frames vary in size; the host is told the largest, so it can
  // size its own buffer and never sees a frame it was not prepared for.
  Camera.setMaxFrameSize(UVC_FRAME_MAX_BYTES);
#endif
  Camera.setFrameSize(VAR_WIDTH, VAR_HEIGHT);
  Camera.setFrameRate(VAR_FPS);

  // The callback only counts. Arming happens in one place, loop(), because a
  // camera has to pace itself to the frame rate it advertised: arming from
  // here would send frames as fast as the endpoint drains them, which measured
  // 180 fps against an advertised 15 and is not what a host asked for.
  Camera.onFrameComplete([]() { framesSent++; });
  Camera.onCommit([](const EspUsbDeviceVideoCommit &commit)
                  {
                    Serial.printf("UVC commit format=%u frame=%u interval=%lu maxFrame=%lu maxPayload=%lu\n",
                                  commit.formatIndex, commit.frameIndex,
                                  (unsigned long)commit.frameInterval,
                                  (unsigned long)commit.maxVideoFrameSize,
                                  (unsigned long)commit.maxPayloadTransferSize);
                    reportFifo("commit");
                  });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = VAR_PID;
  config.manufacturer = "EspUsbDevice";
  config.product = "UVC test camera";
  config.serialNumber = "uvc-1";
  if (!device.begin(config))
  {
    Serial.printf("UVC_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("UVC pid=0x%04x format=%s %ux%u @%ufps maxFrame=%lu\n",
                (unsigned)VAR_PID, VAR_FORMAT == 1 ? "YUY2" : "MJPEG",
                (unsigned)VAR_WIDTH, (unsigned)VAR_HEIGHT, (unsigned)VAR_FPS,
                (unsigned long)Camera.maxFrameSize());
  Serial.printf("UVC bulk=%d isoPacket=%u descriptorLen=%u\n",
                EspUsbDeviceVideo::bulkStreaming() ? 1 : 0,
                (unsigned)EspUsbDeviceVideo::isochronousPacketSize(false),
                (unsigned)Camera.descriptorLength(false));
  // The configuration as sent, so a host-side surprise can be checked against
  // the bytes rather than against what the code was meant to emit.
  const uint8_t *cfg = device.configurationDescriptor(0);
  const uint16_t total = static_cast<uint16_t>(cfg[2] | (cfg[3] << 8));
  Serial.printf("UVC config total=%u interfaces=%u\n", total, cfg[4]);
  for (uint16_t o = 0; o + 2 <= total && cfg[o]; o = static_cast<uint16_t>(o + cfg[o]))
  {
    if (cfg[o + 1] == 0x04)
    {
      Serial.printf("UVC itf num=%u alt=%u eps=%u class=%02x/%02x/%02x\n",
                    cfg[o + 2], cfg[o + 3], cfg[o + 4], cfg[o + 5], cfg[o + 6],
                    cfg[o + 7]);
    }
    else if (cfg[o + 1] == 0x05)
    {
      Serial.printf("UVC ep addr=0x%02x attr=0x%02x mps=%u interval=%u\n",
                    cfg[o + 2], cfg[o + 3],
                    (unsigned)(cfg[o + 4] | (cfg[o + 5] << 8)), cfg[o + 6]);
    }
  }
}

void loop()
{
  const bool streaming = Camera.streaming();
  if (streaming && !wasStreaming)
  {
    // The host selected the streaming alternate setting.
    streamStartedMs = millis();
    nextFrameDueMs = millis();
    framesSent = 0;
    framesFailed = 0;
    bytesSent = 0;
    Serial.println("UVC streaming start");
    reportFifo("stream");
  }
  else if (!streaming && wasStreaming)
  {
    Serial.printf("UVC streaming stop frames=%lu failed=%lu\n",
                  (unsigned long)framesSent, (unsigned long)framesFailed);
  }
  wasStreaming = streaming;

  // One arming point, paced to the advertised rate. A frame that is still in
  // flight when the next one is due is skipped rather than queued, which is
  // what a camera does when the link cannot keep up.
  if (streaming && !Camera.frameInFlight() &&
      static_cast<int32_t>(millis() - nextFrameDueMs) >= 0)
  {
    nextFrameDueMs += 1000UL / VAR_FPS;
    // A long stall would otherwise leave the deadline in the past and send a
    // burst to catch up.
    if (static_cast<int32_t>(millis() - nextFrameDueMs) > 0)
    {
      nextFrameDueMs = millis();
    }
    armNextFrame();
  }

  static uint32_t lastReport = 0;
  if (streaming && millis() - lastReport >= 2000)
  {
    lastReport = millis();
    const uint32_t elapsed = millis() - streamStartedMs;
    if (elapsed > 0)
    {
      Serial.printf("UVC rate frames=%lu failed=%lu fps=%lu.%02lu KBps=%lu\n",
                    (unsigned long)framesSent, (unsigned long)framesFailed,
                    (unsigned long)(framesSent * 1000UL / elapsed),
                    (unsigned long)((framesSent * 100000UL / elapsed) % 100),
                    (unsigned long)(bytesSent / elapsed));
    }
  }
  delay(1);
}
