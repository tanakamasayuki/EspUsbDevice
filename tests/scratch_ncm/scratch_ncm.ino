#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceNet net(device);

static volatile uint32_t rxFrames = 0;
static volatile uint32_t rxBytes = 0;

void setup()
{
  Serial.begin(115200);
  delay(1500);

  net.onFrame([](const uint8_t *data, size_t len)
              {
                rxFrames++;
                rxBytes += len;
              });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4031;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice NCM";
  config.serialNumber = "espusb-ncm";

  Serial.printf("NCM_BEGIN %u %s\n", device.begin(config) ? 1 : 0, device.lastErrorName());
}

void loop()
{
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (now - last >= 2000)
  {
    last = now;
    Serial.printf("NCM_STATE link=%u rx_frames=%lu rx_bytes=%lu\n",
                  net.linkUp() ? 1 : 0,
                  static_cast<unsigned long>(rxFrames),
                  static_cast<unsigned long>(rxBytes));
  }
  delay(1);
}
