// en: Combine a CDC command port, keyboard and FAT RAM disk.
// ja: CDCの操作port・キーボード・FAT RAM diskを組み合わせる。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"


// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceCdcSerial UsbSerial(device);
EspUsbDeviceMsc msc(device);

// en: 48 KB RAM disk: kept small enough that the composite's .bss also fits the ESP32-S2 DRAM budget (a 96 KB disk overflows S2 by ~27 KB at link time; S3/P4 have more DRAM). FAT12 still formats fine at this size.
// ja: 複合機能のbufferも収まるようRAM diskを抑える。大容量には媒体とメモリ配置を再検討する。
static uint8_t storage[48 * 1024];
EspUsbDeviceMscFatRamDisk disk(storage, sizeof(storage));

static char lineBuffer[128];
static size_t lineLength = 0;

// en: Handle one complete line received over USB CDC. "type <text>" is typed out on the HID keyboard; anything else is echoed back over CDC.
// ja: CDCで完成した一行を解釈し、type命令でHIDの文字入力を行う。
static void handleCdcLine(const char *line)
{
  if (strncmp(line, "type ", 5) == 0)
  {
    if (keyboard.write(line + 5) && keyboard.tapKey('\n'))
    {
      UsbSerial.printf("typed: %s\r\n", line + 5);
    }
    else
    {
      UsbSerial.printf("type_failed error=%s\r\n", device.lastErrorName());
    }
    return;
  }
  UsbSerial.printf("echo: %s\r\n", line);
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  // --- MSC: a FAT-formatted RAM disk that shows up as a removable drive. ---
  // en: Prepare the volume before USB exposes it; this operation is not a read-only inspection.
  // ja: USBへ公開する前にvolumeを準備する。読み取りだけの確認操作ではない。
  if (!disk.format("ESPUSB"))
  {
    Serial.println("FAT_FORMAT_FAILED");
    return;
  }
  disk.addTextFile("README.TXT",
                   "EspUsbDevice composite device\r\n"
                   "\r\n"
                   "This drive is also a USB keyboard and a USB serial port.\r\n"
                   "Open the serial port and send: type Hello\r\n");
  msc.vendorID("ESP32");
  msc.productID("COMPOSITE");
  msc.productRevision("1.0");
  msc.mediaPresent(true);
  msc.isWritable(true);
  // en: Attach the medium before starting USB; host and sketch must not edit its filesystem independently.
  // ja: USB開始前に媒体を接続する。ホストとスケッチが別々にfilesystemを書き換えないようにする。
  if (!disk.attach(msc))
  {
    Serial.println("MSC_ATTACH_FAILED");
    return;
  }

  // --- HID keyboard ---
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  // --- CDC serial ---
  UsbSerial.onLineState([](const EspUsbDeviceCdcLineState &state)
                        { Serial.printf("CDC_LINE_STATE dtr=%u rts=%u\n",
                                        state.dtr ? 1 : 0, state.rts ? 1 : 0); });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4030;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Composite";
  config.serialNumber = "espusb-composite";

  // A single begin() brings up all three functions as one composite device.
  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB composite (HID keyboard + CDC serial + MSC) ready");
}

void loop()
{
  // Assemble CDC input into lines and act on each completed line.
  // en: Read only bytes already buffered; application message boundaries must be tracked separately.
  // ja: bufferに届いたbyteを読む。アプリのメッセージの区切りは別に管理する。
  while (UsbSerial.available() > 0)
  {
    const int value = UsbSerial.read();
    if (value < 0)
    {
      break;
    }
    const char c = static_cast<char>(value);
    if (c == '\r')
    {
      continue;
    }
    if (c == '\n')
    {
      lineBuffer[lineLength] = '\0';
      handleCdcLine(lineBuffer);
      lineLength = 0;
      continue;
    }
    if (lineLength < sizeof(lineBuffer) - 1)
    {
      lineBuffer[lineLength++] = c;
    }
  }

  // Periodic heartbeat over CDC so a connected host sees the device is alive.
  static uint32_t lastTickMs = 0;
  const uint32_t now = millis();
  if (UsbSerial.connected() && now - lastTickMs >= 5000)
  {
    lastTickMs = now;
    UsbSerial.printf("tick=%lu (send 'type <text>' to use the keyboard)\r\n",
                     static_cast<unsigned long>(now / 1000));
    UsbSerial.flush();
  }

  delay(1);
}
