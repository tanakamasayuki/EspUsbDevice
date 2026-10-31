#include <EspUsbDevice.h>
#include <atomic>

EspUsbDevice device;
EspUsbDeviceVideo camera(device, "EspUsbDevice Camera"); // Name shown by the host.

constexpr uint16_t width = 160, height = 120;
constexpr uint8_t frameRate = 15;

// The driver borrows this frame until onFrameResult returns it, so the next
// frame is drawn only once frameInFlight() is false.
uint8_t frame[width * height * 2];
std::atomic<uint32_t> sentFrames{0}, failedFrames{0};
std::atomic<bool> committed{false};
uint32_t drawn = 0, nextFrameDue = 0;
bool wasStreaming = false;

// YUY2 colour bars with a block that steps across the bottom once per frame.
// The moving block tells a live stream from a stuck one.
void drawFrame(uint32_t index) {
  // BT.601 bars as {Y, U, V}. YUY2 packs two pixels into Y0 U Y1 V, so a pair
  // of pixels shares one U and one V.
  static const uint8_t bars[8][3] = {
      {235, 128, 128}, {210, 16, 146}, {170, 166, 16}, {145, 54, 34},
      {106, 202, 222}, {81, 90, 240}, {41, 240, 110}, {16, 128, 128}};
  const uint16_t barWidth = width / 8, blockRow = height * 3 / 4, blockWidth = width / 16;
  const uint16_t blockX = uint16_t((index % 16) * blockWidth);
  for (uint16_t y = 0; y < height; y++) {
    uint8_t *row = frame + size_t(y) * width * 2;
    for (uint16_t x = 0; x < width; x += 2) {
      const uint8_t *bar = bars[(x / barWidth) & 7];
      uint8_t luma = bar[0], u = bar[1], v = bar[2];
      if (y >= blockRow) {
        luma = (x >= blockX && x < blockX + blockWidth) ? 235 : 16;
        u = v = 128;
      }
      row[x * 2 + 0] = luma;
      row[x * 2 + 1] = u;
      row[x * 2 + 2] = luma;
      row[x * 2 + 3] = v;
    }
  }
}

void setup() {
  Serial.begin(115200); // Separate debug connection.
  EspUsbVideoConfig config;
  config.transport = EspUsbVideoTransport::Isochronous; // Bulk is also available.
  config.payloadBytes = 512; // Per (micro)frame, including the 2-byte UVC header.
  const esp_err_t configured = camera.configure(config);
  if (configured != ESP_OK) {
    Serial.printf("Video configuration failed: %d\n", int(configured));
    return;
  }
  if (!camera.setFormat(EspUsbDeviceVideoFormat::Yuy2) || !camera.setFrameSize(width, height) ||
      !camera.setFrameRate(frameRate)) {
    Serial.println("Video format rejected");
    return;
  }
  // Runs on the USB worker for every frame the driver returns, whatever the
  // reason. Keep it short: this one only counts.
  camera.onFrameResult([](const EspUsbTransferResult &result) {
    if (result.status == EspUsbTransferStatus::Success) ++sentFrames;
    else ++failedFrames;
  });
  // Also on the USB worker: note the commit here and print it from loop().
  camera.onCommit([](const EspUsbDeviceVideoCommit &) { committed = true; });

  EspUsbDeviceConfig identity;
  identity.manufacturer = "EspUsbDevice";
  identity.product = "EspUsbDevice Camera";
  identity.serialNumber = "camera-1";
  if (!device.begin(identity)) {
    // ESP_ERR_INVALID_SIZE: the IN endpoints do not fit the transmit FIFO, or
    // there are too many endpoints or too large a descriptor (lower
    // payloadBytes, choose Bulk or drop a Function). ESP_ERR_NO_MEM: heap.
    Serial.printf("USB start failed: %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("UVC camera ready: %ux%u YUY2 at %u fps, %u-byte payloads\n", width, height,
      frameRate, unsigned(camera.payloadBufferSize()));
  Serial.println("Open it wherever your OS lists webcams.");
}

void loop() {
  if (committed.exchange(false)) {
    // What the host committed to can differ from what was advertised.
    const EspUsbDeviceVideoCommit commit = camera.commit();
    Serial.printf("Host committed: frame %u, %lu ns interval, payload %lu bytes\n",
        unsigned(commit.frameIndex), static_cast<unsigned long>(commit.frameInterval) * 100,
        static_cast<unsigned long>(commit.maxPayloadTransferSize));
  }
  const bool streaming = camera.streaming();
  if (streaming && !wasStreaming) {
    Serial.println("Streaming started");
    nextFrameDue = millis();
    drawn = 0;
    sentFrames = failedFrames = 0;
  } else if (!streaming && wasStreaming) {
    Serial.printf("Streaming stopped: %lu frames sent, %lu not completed\n",
        static_cast<unsigned long>(sentFrames.load()), static_cast<unsigned long>(failedFrames.load()));
  }
  wasStreaming = streaming;

  // Arm frames from one place, paced to the advertised rate. Arming from the
  // result callback would send as fast as the endpoint drains instead.
  if (streaming && !camera.frameInFlight() && int32_t(millis() - nextFrameDue) >= 0) {
    nextFrameDue += 1000u / frameRate;
    // After a long stall, restart the schedule instead of sending a burst.
    if (int32_t(millis() - nextFrameDue) > 0) nextFrameDue = millis();
    drawFrame(drawn++);
    const EspUsbTransferSubmission submission = camera.submitFrame(frame, sizeof(frame));
    if (!submission) Serial.printf("Frame rejected: %u\n", unsigned(submission.error));
  }
  delay(1);
}
