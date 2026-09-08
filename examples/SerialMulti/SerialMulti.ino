#include "EspUsbDevice.h"

// Two independent USB CDC ACM serial ports on one device.
//
// The host sees two COM ports / two /dev/ttyACM* nodes, each with its own
// buffers, line coding and DTR state. A common use is separating a machine
// protocol from a human console so log output never corrupts a command stream.
//
// How many ports fit is decided by the controller, not by this sketch: each
// port costs two IN endpoints (notification + data). An S2/S3 has four
// non-control IN endpoints, so two ports is its maximum and nothing else fits
// beside them. The ESP32-P4's high-speed controller has seven, which leaves
// room for two ports plus HID and Vendor, or for three ports on their own.
// Exceeding the budget makes begin() fail with ESP_ERR_INVALID_SIZE before the
// PHY is started, so the host never sees a half-built device.

EspUsbDevice device;

// The name is what the host shows for each port. Give the ports distinct
// names: they are otherwise identical ACM functions, and without names a user
// has no way to tell which node is which.
EspUsbDeviceCdcSerial Console(device, "Console");
EspUsbDeviceCdcSerial DataLink(device, "Data Link");

// The third port exists only where the endpoint budget has room for it. Two
// ports already spend all four of the S2/S3 controller's non-control IN
// endpoints; the P4's high-speed controller has seven, so a third fits with one
// to spare. Registering it on an S3 would not fail quietly - begin() would
// return ESP_ERR_INVALID_SIZE and the device would never enumerate - so the
// port is compiled in only where it can actually exist.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define SERIAL_MULTI_HAS_THIRD_PORT 1
EspUsbDeviceCdcSerial Telemetry(device, "Telemetry");
#else
#define SERIAL_MULTI_HAS_THIRD_PORT 0
#endif

static uint32_t lastTickMs = 0;

static void echoLine(EspUsbDeviceCdcSerial &port, const char *label)
{
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
  // The P4 defaults to its high-speed controller, which is where the third
  // port's endpoints come from. Selecting FullSpeed here would drop the budget
  // back to the S3's four IN endpoints and make begin() refuse the third port.
  config.controller = EspUsbController::Auto;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice DualSerial";
  // A stable serial number is what lets a host keep the same port names across
  // replugs, and lets several of these boards coexist on one machine.
  config.serialNumber = "espusb-dualserial-0001";

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
