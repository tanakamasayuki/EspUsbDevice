// en: Receive a length-delimited firmware image through CDC.
// ja: CDCから長さを指定したfirmware imageを受け取る。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"


// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceCdcSerial port(device, "Firmware");
EspUsbDeviceFirmwareUpdate update;

static size_t expected = 0;
static size_t received = 0;
static char command[32];
static uint8_t commandLength = 0;
static uint32_t reportedKiB = 0;

static void fail(const char *reason)
{
  update.abort();
  expected = 0;
  received = 0;
  commandLength = 0;
  port.printf("ERR %s\n", reason);
  port.flush();
  Serial.printf("Update failed: %s\n", reason);
}

// en: Read the "FW <size>" line a byte at a time. Nothing here is time critical: the transfer has not started yet.
// ja: サイズ命令を一文字ずつ読み、binary本体を受け取る状態と分ける。
static void readCommand()
{
  // en: Read only bytes already buffered; application message boundaries must be tracked separately.
  // ja: bufferに届いたbyteを読む。アプリのメッセージの区切りは別に管理する。
  while (port.available() > 0)
  {
    const int byte = port.read();
    if (byte < 0)
    {
      return;
    }
    if (byte == '\n' || byte == '\r')
    {
      if (commandLength == 0)
      {
        continue;
      }
      command[commandLength] = '\0';
      commandLength = 0;

      unsigned long size = 0;
      if (sscanf(command, "FW %lu", &size) != 1 || size == 0)
      {
        fail("expected FW <size>");
        return;
      }
      if (!EspUsbDeviceFirmwareUpdate::available())
      {
        fail("no OTA partition");
        return;
      }
      // The size is known here, so an image too big for the partition is
      // refused before the host sends a single byte of it.
      // en: Begin only for the intended application image and size; this can write an update partition.
      // ja: 意図したapplication imageと長さで開始する。更新partitionへの書き込みを伴う。
      if (!update.begin(size))
      {
        fail(update.lastErrorName());
        return;
      }
      expected = size;
      received = 0;
      reportedKiB = 0;
      Serial.printf("Receiving %lu bytes into %s\n", size,
                    EspUsbDeviceFirmwareUpdate::targetLabel());
      port.print("READY\n");
      port.flush();
      return;
    }
    if (commandLength < sizeof(command) - 1)
    {
      command[commandLength++] = static_cast<char>(byte);
    }
  }
}

// en: The image itself. This runs in loop(), not in a USB callback: flash erase takes milliseconds and every class callback in this library runs on the usbd task, where that time is taken from the whole device.
// ja: Flash操作を含む受信処理はloop()で行い、USB callbackへ移さない。
static void readImage()
{
  uint8_t buffer[512];
  while (port.available() > 0 && received < expected)
  {
    const size_t want = expected - received < sizeof(buffer) ? expected - received : sizeof(buffer);
    const size_t got = port.read(buffer, want);
    if (got == 0)
    {
      return;
    }
    if (!update.write(buffer, got))
    {
      fail(update.lastErrorName());
      return;
    }
    received += got;

    const uint32_t kiB = received / 1024;
    if (kiB != reportedKiB)
    {
      reportedKiB = kiB;
      Serial.printf("%u KiB\n", static_cast<unsigned>(kiB));
    }
  }

  if (received < expected)
  {
    return;
  }

  // end() checks the image header, the length and the checksum before it moves
  // the boot partition, so a truncated or corrupt upload fails here rather than
  // at the next boot.
  // en: Finishing reception and passing image verification are different steps.
  // ja: 受信完了とimage検証の成功は別の段階。
  if (!update.end())
  {
    fail(update.lastErrorName());
    return;
  }
  expected = 0;
  port.print("OK\n");
  port.flush();
  Serial.println("Verified - restarting into the new firmware");
  delay(100); // let the reply reach the host before the port disappears
  esp_restart();
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
  else
  {
    Serial.printf("Update target: %s (%u bytes)\n",
                  EspUsbDeviceFirmwareUpdate::targetLabel(),
                  static_cast<unsigned>(EspUsbDeviceFirmwareUpdate::capacity()));
  }

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin())
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Ready. python3 firmware_cdc.py <port> <firmware.bin>");
}

void loop()
{
  if (expected == 0)
  {
    readCommand();
  }
  else
  {
    readImage();
  }

  static bool confirmed = false;
  if (!confirmed && device.ready())
  {
    confirmed = true;
    EspUsbDeviceFirmwareUpdate::markValid();
  }
  delay(1);
}
