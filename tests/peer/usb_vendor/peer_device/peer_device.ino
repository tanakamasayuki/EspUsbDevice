#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);

static volatile uint32_t rxCount = 0;
static volatile uint32_t controlCount = 0;
static volatile uint32_t rxLastChunk = 0;
static volatile uint32_t rxChunks = 0;
// Echo is what the transfer tests observe, but it doubles as backpressure: a
// bulk-OUT burst nobody reads back would fill the device TX path and change what
// the RX test is measuring. The 'e' command turns it off for those cases.
static volatile bool echoEnabled = true;

static void processVendorRx()
{
  size_t available = Vendor.available();
  uint8_t buffer[64];
  while (available > 0)
  {
    const size_t chunk = Vendor.read(buffer, min(available, sizeof(buffer)));
    if (chunk == 0)
    {
      break;
    }
    rxCount += chunk;
    rxChunks++;
    rxLastChunk = chunk;
    if (echoEnabled)
    {
      Vendor.write(reinterpret_cast<const uint8_t *>("echo:"), 5);
      Vendor.write(buffer, chunk);
      Vendor.flush();
    }
    available = Vendor.available();
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  Vendor.onRx([](size_t)
              { processVendorRx(); });

  Vendor.onControlRequest([](const EspUsbDeviceVendorControlRequest &request)
                          {
                            controlCount++;
                            static const char info[] = "EspUsbDeviceVendor";
                            if ((request.bmRequestType & 0x80) && request.bRequest == 0x10)
                            {
                              return Vendor.sendControlResponse(request, info, min(static_cast<size_t>(request.wLength), sizeof(info) - 1));
                            }
                            if (!(request.bmRequestType & 0x80) && request.bRequest == 0x11)
                            {
                              return Vendor.sendControlResponse(request);
                            }
                            return false;
                          });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4019;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice USB Vendor";
  config.serialNumber = "espusb-usb-vendor";
  config.webusbEnabled = true;
  config.webusbUrl = "example.com/espusbdevice";
  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
}

void loop()
{
  processVendorRx();
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.println("DEVICE_READY");
    }
    else if (command == 's')
    {
      Serial.printf("DEVICE_STATUS rx=%lu control=%lu\n",
                    static_cast<unsigned long>(rxCount),
                    static_cast<unsigned long>(controlCount));
    }
    else if (command == 'b')
    {
      Serial.printf("DEVICE_RX_BYTES rx=%lu chunks=%lu last=%lu\n",
                    static_cast<unsigned long>(rxCount),
                    static_cast<unsigned long>(rxChunks),
                    static_cast<unsigned long>(rxLastChunk));
    }
    else if (command == 'e')
    {
      echoEnabled = !echoEnabled;
      Serial.printf("DEVICE_ECHO %u\n", echoEnabled ? 1 : 0);
    }
    else if (command == 'z')
    {
      rxCount = 0;
      rxChunks = 0;
      rxLastChunk = 0;
      Serial.println("DEVICE_RX_RESET");
    }
  }
  delay(1);
}
