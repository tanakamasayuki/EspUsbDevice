#include "EspUsbDevice.h"

// Device side for the two-port CDC test.
//
// Two ACM functions is the ESP32-S3 maximum: each costs two IN endpoints
// (notification + data) and the controller has four non-control IN endpoints.
// Nothing else fits beside them, which is exactly why this sketch registers
// nothing else.

EspUsbDevice device;
EspUsbDeviceCdcSerial Port0(device, "Console");
EspUsbDeviceCdcSerial Port1(device, "Data Link");

static bool started = false;
static esp_err_t beginError = ESP_OK;
static uint32_t rxCount0 = 0;
static uint32_t rxCount1 = 0;

static void drain(EspUsbDeviceCdcSerial &port, const char *label, uint32_t &counter)
{
  if (port.available() <= 0)
  {
    return;
  }
  char line[64];
  size_t length = 0;
  while (port.available() > 0 && length < sizeof(line) - 1)
  {
    const int value = port.read();
    if (value < 0)
    {
      break;
    }
    if (value == '\n' || value == '\r')
    {
      continue;
    }
    line[length++] = static_cast<char>(value);
  }
  line[length] = '\0';
  if (length > 0)
  {
    counter++;
    Serial.printf("%s %s\n", label, line);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4015;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice DualSerial";
  config.serialNumber = "espusb-dualserial";

  started = device.begin(config);
  beginError = device.lastError();
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.println(started ? "DEVICE_READY" : "DEVICE_NOT_READY");
    }
    else if (command == 'b')
    {
      Serial.printf("DEVICE_BEGIN %s %s\n",
                    started ? "ok" : "ng",
                    device.lastErrorName());
      (void)beginError;
    }
    else if (command == 'p')
    {
      // Port index is the TinyUSB instance the object drives, taken from the
      // order the functions appear in the configuration descriptor.
      Serial.printf("DEVICE_PORTS max=%u port0=%u port1=%u\n",
                    static_cast<unsigned>(EspUsbDevice::maxCdcPorts()),
                    Port0.port(),
                    Port1.port());
    }
    else if (command == 'c')
    {
      Serial.printf("DEVICE_CONNECTED port0=%u port1=%u\n",
                    Port0.connected() ? 1 : 0,
                    Port1.connected() ? 1 : 0);
    }
    else if (command == 'r')
    {
      Serial.printf("DEVICE_RXCOUNT port0=%lu port1=%lu\n",
                    static_cast<unsigned long>(rxCount0),
                    static_cast<unsigned long>(rxCount1));
    }
    else if (command == 'd')
    {
      const size_t written = Port0.write(
          reinterpret_cast<const uint8_t *>("from port zero\n"), 15);
      Serial.printf("DEVICE_TX0 %u\n", written == 15 ? 1 : 0);
    }
    else if (command == 'D')
    {
      const size_t written = Port1.write(
          reinterpret_cast<const uint8_t *>("from port one\n"), 14);
      Serial.printf("DEVICE_TX1 %u\n", written == 14 ? 1 : 0);
    }
    else if (command == 'l')
    {
      Serial.printf("DEVICE_LINE_CODING port0=%lu port1=%lu\n",
                    static_cast<unsigned long>(Port0.lineCoding().baud),
                    static_cast<unsigned long>(Port1.lineCoding().baud));
    }
  }

  drain(Port0, "DEVICE_RX0", rxCount0);
  drain(Port1, "DEVICE_RX1", rxCount1);

  device.task();
  delay(1);
}
