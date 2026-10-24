// en: Create a CDC serial function with receive and line-state diagnostics.
// ja: 受信とline stateを観察できるCDCシリアル機能を作る。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceCdcSerial UsbSerial(device);

static uint32_t lastMessageMs = 0;
static uint32_t lineCodingCount = 0;
static uint32_t lineStateCount = 0;

static void printLineCoding(const EspUsbDeviceCdcLineCoding &coding)
{
  Serial.printf("CDC_LINE_CODING baud=%lu stop=%u parity=%u data=%u\n",
                static_cast<unsigned long>(coding.baud),
                coding.stopBits,
                coding.parity,
                coding.dataBits);
}

static void printLineState(const EspUsbDeviceCdcLineState &state)
{
  Serial.printf("CDC_LINE_STATE dtr=%u rts=%u\n",
                state.dtr ? 1 : 0,
                state.rts ? 1 : 0);
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  UsbSerial.onLineCoding([](const EspUsbDeviceCdcLineCoding &coding)
                         {
                           lineCodingCount++;
                           printLineCoding(coding);
                         });

  UsbSerial.onLineState([](const EspUsbDeviceCdcLineState &state)
                        {
                          lineStateCount++;
                          printLineState(state);
                        });

  // en: This callback runs on the USB task; keep work bounded and hand larger processing to the application.
  // ja: USBタスクで動くcallback。処理を短く保ち、大きな仕事はアプリ側へ渡す。
  UsbSerial.onRx([](size_t available)
                 {
                   Serial.printf("CDC_RX available=%u\n", static_cast<unsigned>(available));
                 });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4016;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Serial";
  config.serialNumber = "espusb-serial";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB CDC serial ready");
}

void loop()
{
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
      UsbSerial.print("\r\n");
      continue;
    }
    UsbSerial.print("echo: ");
    UsbSerial.write(static_cast<uint8_t>(c));
    UsbSerial.print("\r\n");
  }

  const uint32_t now = millis();
  if (UsbSerial.connected() && now - lastMessageMs >= 3000)
  {
    lastMessageMs = now;
    UsbSerial.printf("tick=%lu line_coding=%lu line_state=%lu\r\n",
                     static_cast<unsigned long>(now / 1000),
                     static_cast<unsigned long>(lineCodingCount),
                     static_cast<unsigned long>(lineStateCount));
    UsbSerial.flush();
  }

  delay(1);
}
