#include "EspUsbDevice.h"

// Manual test / demo: USB CDC-NCM network device with a built-in DHCP server.
// The board appears to the USB host as a network adapter; the host is handed an
// address on 192.168.7.0/24 and can reach the device at 192.168.7.1.
//
// Manual because it needs the device's USB-OTG port cabled to a PC (not the peer
// rig) and host-side networking. See test_usb_ncm.py and README.md.

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
                (void)data;
                rxFrames++;
                rxBytes += len;
              });

  // Device acts as the gateway (192.168.7.1) and runs a DHCP server so the PC
  // gets an address automatically. DHCP is opt-in: dhcpClient(true) or a bare
  // ipConfig() (static, no server) are the alternatives.
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
