#include "EspUsbDevice.h"

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

  UsbSerial.onRx([](size_t available)
                 {
                   Serial.printf("CDC_RX available=%u\n", static_cast<unsigned>(available));
                 });

  EspUsbDeviceConfig config;
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4016;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Serial";
  config.serialNumber = "espusb-serial";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB CDC serial ready");
}

void loop()
{
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
