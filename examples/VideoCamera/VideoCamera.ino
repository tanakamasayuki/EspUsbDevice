// en: Generate test images and stream them as USB camera frames.
// ja: 試験画像を生成し、USBカメラのframeとして流す。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceVideo Camera(device, "EspUsbDevice Camera");

static const uint16_t kWidth = 160;
static const uint16_t kHeight = 120;
static const uint8_t kFrameRate = 15;

// en: One frame, rebuilt between transfers. The class driver reads this buffer payload by payload while a transfer is in flight, so it must not be touched until the completion callback has run - which is why the next frame is only drawn once frameInFlight() is false.
// ja: frame転送中はbufferを変更せず、frameInFlight()がfalseになってから描き直す。
static uint8_t frameBuffer[kWidth * kHeight * 2];
static uint32_t frameCount = 0;
static uint32_t nextFrameDueMs = 0;
static bool wasStreaming = false;

// en: YUY2 colour bars with a block that steps across the bottom once per frame. The moving block is the point: a still image cannot tell a live stream from a stuck one.
// ja: YUY2の試験画像に動く部分を加え、静止画像だけでは分からないstreamの停止を見分ける。
static void drawFrame(uint32_t index)
{
  // en: BT.601 colour bars as {Y, U, V}. YUY2 carries one U and one V per pair of pixels and both matter: hold V at 128 and the picture still looks like a plausible test pattern with every colour wrong.
  // ja: YUY2では2画素で色差成分を共有する。画素ごとに独立したRGBとして扱わない。
  static const uint8_t bars[8][3] = {
      {235, 128, 128}, {210, 16, 146}, {170, 166, 16}, {145, 54, 34},
      {106, 202, 222}, {81, 90, 240},  {41, 240, 110}, {16, 128, 128}};
  const uint16_t barWidth = kWidth / 8;
  const uint16_t blockRow = (kHeight * 3) / 4;
  const uint16_t blockWidth = kWidth / 16;
  const uint16_t blockX = static_cast<uint16_t>((index % 16) * blockWidth);
  for (uint16_t y = 0; y < kHeight; y++)
  {
    uint8_t *row = &frameBuffer[static_cast<size_t>(y) * kWidth * 2];
    // YUY2 packs two pixels into four bytes: Y0 U Y1 V.
    for (uint16_t x = 0; x < kWidth; x += 2)
    {
      const uint8_t *bar = bars[(x / barWidth) & 7];
      uint8_t luma = bar[0];
      uint8_t u = bar[1];
      uint8_t v = bar[2];
      // Below the bars, a white block steps one cell per frame against black.
      const bool inBlock = x >= blockX && x < blockX + blockWidth;
      if (y >= blockRow)
      {
        luma = inBlock ? 235 : 16;
        u = 128;
        v = 128;
      }
      row[x * 2 + 0] = luma;
      row[x * 2 + 1] = u;
      row[x * 2 + 2] = luma;
      row[x * 2 + 3] = v;
    }
  }
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(2000);

  Camera.setFormat(EspUsbDeviceVideoFormat::Yuy2);
  Camera.setFrameSize(kWidth, kHeight);
  Camera.setFrameRate(kFrameRate);

  // en: Fires on the USB device task when the last payload of a frame has gone. Keep it short; this one only counts.
  // ja: frame完了はUSBタスクから通知される。ここでは記録だけを行い短く戻る。
  Camera.onFrameComplete([]() { frameCount++; });
  // en: The host negotiates before it starts, and what it committed to can differ from what was advertised. A camera that can change resolution reads the committed frame index here.
  // ja: ホストの確定formatは提示した候補と異なる場合がある。commitされた条件で解釈する。
  Camera.onCommit([](const EspUsbDeviceVideoCommit &commit)
                  {
                    Serial.printf("Host committed: frame %u, %lu ns interval, payload %lu bytes\n",
                                  commit.frameIndex,
                                  (unsigned long)commit.frameInterval * 100,
                                  (unsigned long)commit.maxPayloadTransferSize);
                  });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4000;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice Camera";
  config.serialNumber = "camera-1";
  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    // en: ESP_ERR_NO_MEM here means the controller's transmit FIFO cannot back the isochronous endpoint alongside everything else in the configuration. Lower CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE, or drop another function.
    // ja: 送信FIFO等の資源が足りない場合は、frame転送設定や複合機能の構成を見直す。
    Serial.printf("begin() failed: %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("UVC camera ready: %ux%u YUY2 at %u fps, %u-byte payloads\n",
                kWidth, kHeight, kFrameRate,
                (unsigned)EspUsbDeviceVideo::payloadBufferSize());
  Serial.println("Open it wherever your OS lists webcams.");
}

void loop()
{
  const bool streaming = Camera.streaming();
  if (streaming && !wasStreaming)
  {
    Serial.println("Streaming started");
    nextFrameDueMs = millis();
    frameCount = 0;
  }
  else if (!streaming && wasStreaming)
  {
    Serial.printf("Streaming stopped after %lu frames\n", (unsigned long)frameCount);
  }
  wasStreaming = streaming;

  // en: One place arms frames, paced to the advertised rate. Arming from the completion callback instead would send as fast as the endpoint drains, which is not the frame rate the host was promised.
  // ja: 次のframeはloop()側で時刻制御する。転送が空く速さだけでframe rateを決めない。
  if (streaming && !Camera.frameInFlight() &&
      static_cast<int32_t>(millis() - nextFrameDueMs) >= 0)
  {
    nextFrameDueMs += 1000UL / kFrameRate;
    if (static_cast<int32_t>(millis() - nextFrameDueMs) > 0)
    {
      // en: A long stall would otherwise leave the deadline behind and send a burst to catch up.
      // ja: 長い停止後の追い付き連送を避け、次の時刻を現在へ合わせる。
      nextFrameDueMs = millis();
    }
    drawFrame(frameCount);
    // en: Keep this frame unchanged until the transfer is no longer in flight.
    // ja: 転送中でなくなるまで、このframeを上書きしない。
    Camera.sendFrame(frameBuffer, sizeof(frameBuffer));
  }
  delay(1);
}
