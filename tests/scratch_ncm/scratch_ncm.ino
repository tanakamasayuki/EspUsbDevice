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
              { rxFrames++; rxBytes += len; });

  // Device is the gateway and hands the PC an address via DHCP (192.168.7.x).
  net.ipConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1), IPAddress(255, 255, 255, 0));
  net.dhcpServer(true);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4031;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice NCM";
  config.serialNumber = "espusb-ncm";

  const bool ok = device.begin(config);
  Serial.printf("NCM_BEGIN %u %s\n", ok ? 1 : 0, device.lastErrorName());
  if (ok)
  {
    Serial.printf("NCM_NET %u ip=%s\n", net.beginNetwork() ? 1 : 0, net.localIP().toString().c_str());
  }
}

void loop()
{
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (now - last >= 2000)
  {
    last = now;
    Serial.printf("NCM_STATE link=%u net=%u ip=%s rx_frames=%lu rx_bytes=%lu\n",
                  net.linkUp() ? 1 : 0, net.networkUp() ? 1 : 0,
                  net.localIP().toString().c_str(),
                  static_cast<unsigned long>(rxFrames),
                  static_cast<unsigned long>(rxBytes));
  }
  delay(1);
}
