// en: Echo vendor bulk data and answer explicit control requests.
// ja: Vendor bulkデータをechoし、定義したcontrol要求へ応答する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceVendor UsbVendor(device);

static uint32_t lastStatusMs = 0;
static uint32_t rxCount = 0;
static uint32_t controlCount = 0;

static void printHex(const uint8_t *data, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    if (i)
    {
      Serial.print(' ');
    }
    Serial.printf("%02x", data[i]);
  }
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  // en: This callback runs on the USB task; keep work bounded and hand larger processing to the application.
  // ja: USBタスクで動くcallback。処理を短く保ち、大きな仕事はアプリ側へ渡す。
  UsbVendor.onRx([](size_t available)
                 {
                   uint8_t buffer[64];
                   while (available > 0)
                   {
                     // en: Read only bytes already buffered; application message boundaries must be tracked separately.
                     // ja: bufferに届いたbyteを読む。アプリのメッセージの区切りは別に管理する。
                     const size_t chunk = UsbVendor.read(buffer, min(available, sizeof(buffer)));
                     if (chunk == 0)
                     {
                       break;
                     }
                     rxCount += chunk;
                     Serial.print("VENDOR_RX ");
                     Serial.print(chunk);
                     Serial.print(" ");
                     printHex(buffer, chunk);
                     Serial.println();

                     UsbVendor.write(reinterpret_cast<const uint8_t *>("echo: "), 6);
                     UsbVendor.write(buffer, chunk);
                     UsbVendor.write(reinterpret_cast<const uint8_t *>("\r\n"), 2);
                     UsbVendor.flush();
                     available = UsbVendor.available();
                   }
                 });

  // en: Check request direction and fields, and answer only the protocol implemented by this example.
  // ja: 要求の方向とfieldを確認し、この例が実装するprotocolだけへ応答する。
  UsbVendor.onControlRequest([](const EspUsbDeviceVendorControlRequest &request)
                             {
                               controlCount++;
                               Serial.printf("VENDOR_CONTROL stage=%u type=0x%02x request=0x%02x value=%u index=%u length=%u\n",
                                             request.stage,
                                             request.bmRequestType,
                                             request.bRequest,
                                             request.wValue,
                                             request.wIndex,
                                             request.wLength);

                               static const char info[] = "EspUsbDeviceVendor";
                               if ((request.bmRequestType & 0x80) && request.bRequest == 0x01)
                               {
                                 return UsbVendor.sendControlResponse(request, info, min(static_cast<size_t>(request.wLength), sizeof(info) - 1));
                               }
                               if (!(request.bmRequestType & 0x80) && request.bRequest == 0x02)
                               {
                                 return UsbVendor.sendControlResponse(request);
                               }
                               return false;
                             });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4019;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice USBVendor";
  config.serialNumber = "espusb-vendor";
  config.webusbEnabled = true;
  config.webusbUrl = "example.com/espusbdevice";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB vendor device ready");
}

void loop()
{
  const uint32_t now = millis();
  if (UsbVendor.mounted() && now - lastStatusMs >= 3000)
  {
    lastStatusMs = now;
    UsbVendor.printf("status rx=%lu control=%lu\r\n",
                     static_cast<unsigned long>(rxCount),
                     static_cast<unsigned long>(controlCount));
    UsbVendor.flush();
  }
  delay(1);
}
