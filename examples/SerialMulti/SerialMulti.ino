// en: Expose independent CDC serial ports on one USB device.
// ja: 一つのUSB機器に独立した複数のCDC portを公開する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"


// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;

// en: The name is what the host shows for each port. Give the ports distinct names: they are otherwise identical ACM functions, and without names a user has no way to tell which node is which.
// ja: ホストに表示するport名を別々にし、同じCDC形式でも利用者が区別できるようにする。
EspUsbDeviceCdcSerial Console(device, "Console");
EspUsbDeviceCdcSerial DataLink(device, "Data Link");

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define SERIAL_MULTI_HAS_THIRD_PORT 1
EspUsbDeviceCdcSerial Telemetry(device, "Telemetry");
#else
#define SERIAL_MULTI_HAS_THIRD_PORT 0
#endif

static uint32_t lastTickMs = 0;

static void echoLine(EspUsbDeviceCdcSerial &port, const char *label)
{
  // en: Read only bytes already buffered; application message boundaries must be tracked separately.
  // ja: bufferに届いたbyteを読む。アプリのメッセージの区切りは別に管理する。
  while (port.available() > 0)
  {
    const int value = port.read();
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
      port.print("\r\n");
      continue;
    }
    Serial.printf("%s RX %c\n", label, c);
    port.printf("%s: %c\r\n", label, c);
  }
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  Console.onLineState([](const EspUsbDeviceCdcLineState &state)
                      {
                        Serial.printf("CONSOLE dtr=%u rts=%u\n",
                                      state.dtr ? 1 : 0, state.rts ? 1 : 0);
                      });

  DataLink.onLineState([](const EspUsbDeviceCdcLineState &state)
                       {
                         Serial.printf("DATALINK dtr=%u rts=%u\n",
                                       state.dtr ? 1 : 0, state.rts ? 1 : 0);
                       });

#if SERIAL_MULTI_HAS_THIRD_PORT
  Telemetry.onLineState([](const EspUsbDeviceCdcLineState &state)
                        {
                          Serial.printf("TELEMETRY dtr=%u rts=%u\n",
                                        state.dtr ? 1 : 0, state.rts ? 1 : 0);
                        });
#endif

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4018;
  // en: The P4 defaults to its high-speed controller, which is where the third port's endpoints come from. Selecting FullSpeed here would drop the budget back to the S3's four IN endpoints and make begin() refuse the third port.
  // ja: controllerによってendpoint資源が異なる。複数port構成を変えたら開始結果を確認する。
  config.controller = EspUsbController::Auto;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice DualSerial";
  // en: A stable serial number is what lets a host keep the same port names across replugs, and lets several of these boards coexist on one machine.
  // ja: 機器のserial番号を安定させ、ホストが再接続や複数台を識別する手掛かりにする。
  config.serialNumber = "espusb-dualserial-0001";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

#if SERIAL_MULTI_HAS_THIRD_PORT
  Serial.printf("USB CDC ready: console=port%u data=port%u telemetry=port%u capacity=%u\n",
                Console.port(), DataLink.port(), Telemetry.port(),
                EspUsbDevice::maxCdcPorts());
#else
  Serial.printf("USB CDC ready: console=port%u data=port%u capacity=%u\n",
                Console.port(), DataLink.port(),
                EspUsbDevice::maxCdcPorts());
#endif
}

void loop()
{
  echoLine(Console, "CONSOLE");
  echoLine(DataLink, "DATALINK");
#if SERIAL_MULTI_HAS_THIRD_PORT
  echoLine(Telemetry, "TELEMETRY");
#endif

  const uint32_t now = millis();
  if (now - lastTickMs >= 3000)
  {
    lastTickMs = now;
    if (Console.connected())
    {
      Console.printf("console tick=%lu\r\n",
                     static_cast<unsigned long>(now / 1000));
    }
    if (DataLink.connected())
    {
      DataLink.printf("data tick=%lu\r\n",
                      static_cast<unsigned long>(now / 1000));
    }
#if SERIAL_MULTI_HAS_THIRD_PORT
    if (Telemetry.connected())
    {
      Telemetry.printf("telemetry tick=%lu\r\n",
                       static_cast<unsigned long>(now / 1000));
    }
#endif
  }

  delay(1);
}
