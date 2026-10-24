// en: Receive firmware through Vendor bulk with control-plane commands.
// ja: Vendor bulkとcontrol命令を使ってfirmwareを受け取る。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"


// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
EspUsbDeviceFirmwareUpdate update;

static constexpr uint8_t REQUEST_START = 0x01;
static constexpr uint8_t REQUEST_COMMIT = 0x02;
static constexpr uint8_t REQUEST_ABORT = 0x03;
static constexpr uint8_t REQUEST_STATUS = 0x04;

// en: State the control requests set and loop() acts on. Control requests and onRx() both run on the usbd task, so nothing here may touch flash.
// ja: control要求の通知とFlash操作を分離し、loop()で保留した仕事を実行する。
static volatile bool startRequested = false;
static volatile bool commitRequested = false;
static volatile bool abortRequested = false;
static volatile uint32_t requestedSize = 0;
static volatile uint32_t lastError = 0;

static size_t expected = 0;
static uint32_t reportedKiB = 0;

static void statusReply(uint8_t reply[8])
{
  const uint32_t written = static_cast<uint32_t>(update.written());
  reply[0] = update.active() ? 1 : 0;
  reply[1] = static_cast<uint8_t>(lastError & 0xff);
  reply[2] = 0;
  reply[3] = 0;
  reply[4] = static_cast<uint8_t>(written & 0xff);
  reply[5] = static_cast<uint8_t>((written >> 8) & 0xff);
  reply[6] = static_cast<uint8_t>((written >> 16) & 0xff);
  reply[7] = static_cast<uint8_t>((written >> 24) & 0xff);
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  if (!EspUsbDeviceFirmwareUpdate::available())
  {
    Serial.println("NO_OTA_PARTITION - select a partition scheme with two app partitions");
  }

  // en: Check request direction and fields, and answer only the protocol implemented by this example.
  // ja: 要求の方向とfieldを確認し、この例が実装するprotocolだけへ応答する。
  vendor.onControlRequest(
      [](const EspUsbDeviceVendorControlRequest &request) -> bool
      {
        // en: Only the setup stage carries a decision; the data and ack stages are the stack finishing what was decided here.
        // ja: 要求の判断はSETUP段階で行い、後続stageと同じ処理を繰り返さない。
        if (request.stage != ESP_USB_DEVICE_CONTROL_STAGE_SETUP)
        {
          return true;
        }
        switch (request.bRequest)
        {
        case REQUEST_START:
          requestedSize = static_cast<uint32_t>(request.wValue) |
                          (static_cast<uint32_t>(request.wIndex) << 16);
          startRequested = true;
          return vendor.sendControlResponse(request);
        case REQUEST_COMMIT:
          commitRequested = true;
          return vendor.sendControlResponse(request);
        case REQUEST_ABORT:
          abortRequested = true;
          return vendor.sendControlResponse(request);
        case REQUEST_STATUS:
        {
          static uint8_t reply[8];
          statusReply(reply);
          return vendor.sendControlResponse(request, reply, sizeof(reply));
        }
        default:
          return false; // stall anything else
        }
      });

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin())
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Ready. python3 firmware_vendor.py <firmware.bin>");
}

// en: Everything that touches flash happens here, on the sketch's own task. The bulk endpoint NAKs while this runs, which is the host slowing down rather than the device stalling.
// ja: Flashを扱う処理はスケッチ側のtaskで行う。USB callbackは要求を渡す。
static void drainBulk()
{
  uint8_t buffer[512];
  // en: Read only bytes already buffered; application message boundaries must be tracked separately.
  // ja: bufferに届いたbyteを読む。アプリのメッセージの区切りは別に管理する。
  while (vendor.available() > 0)
  {
    const size_t got = vendor.read(buffer, sizeof(buffer));
    if (got == 0)
    {
      return;
    }
    if (!update.active())
    {
      continue; // data with no START in front of it
    }
    if (!update.write(buffer, got))
    {
      lastError = static_cast<uint32_t>(update.lastError());
      Serial.printf("Write failed: %s\n", update.lastErrorName());
      return;
    }
    const uint32_t kiB = update.written() / 1024;
    if (kiB != reportedKiB)
    {
      reportedKiB = kiB;
      Serial.printf("%u KiB\n", static_cast<unsigned>(kiB));
    }
  }
}

void loop()
{
  if (startRequested)
  {
    startRequested = false;
    expected = requestedSize;
    reportedKiB = 0;
    lastError = 0;
    // en: Begin only for the intended application image and size; this can write an update partition.
    // ja: 意図したapplication imageと長さで開始する。更新partitionへの書き込みを伴う。
    if (!update.begin(expected))
    {
      lastError = static_cast<uint32_t>(update.lastError());
      Serial.printf("Start failed: %s\n", update.lastErrorName());
    }
    else
    {
      Serial.printf("Receiving %u bytes into %s\n", static_cast<unsigned>(expected),
                    EspUsbDeviceFirmwareUpdate::targetLabel());
    }
  }

  if (abortRequested)
  {
    abortRequested = false;
    update.abort();
    Serial.println("Aborted");
  }

  drainBulk();

  if (commitRequested)
  {
    commitRequested = false;
    if (update.end())
    {
      Serial.println("Verified - restarting into the new firmware");
      delay(100); // let the control transfer's ack reach the host
      esp_restart();
    }
    lastError = static_cast<uint32_t>(update.lastError());
    Serial.printf("Commit failed: %s\n", update.lastErrorName());
  }

  static bool confirmed = false;
  if (!confirmed && device.ready())
  {
    confirmed = true;
    EspUsbDeviceFirmwareUpdate::markValid();
  }
  delay(1);
}
