// Does the ROM come back on the same connector an EspUsbDevice device was on?
//
// On ESP32-S3 one internal PHY and one pin pair (GPIO19/20) are shared between
// USB-Serial-JTAG and USB-OTG. begin() takes them for OTG, and a reset does
// *not* hand them back - the selection is in RTC_CNTL_USB_CONF_REG, which is in
// the RTC domain and survives a software reset. rebootToBootloader() hands them
// back explicitly; this runs the whole sequence on a board whose native USB is
// the only cable to the host, and prints where it is at each step so the
// host-side observation can be lined up against it.
#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware");

#ifndef S3_CABLE_ACTION
#define S3_CABLE_ACTION 0   // 0 = rebootToBootloader, 1 = rebootToRomDfu
#endif

void setup()
{
  Serial.begin(115200);
  delay(1500);
  Serial.printf("S3CABLE_BOOT reason=%d action=%d\n", (int)esp_reset_reason(), S3_CABLE_ACTION);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4095;
  config.manufacturer = "EspUsbDevice";
  config.product = "S3 single cable";
  config.serialNumber = "s3-cable-1";

  if (!device.begin(config))
  {
    Serial.printf("S3CABLE_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("S3CABLE_USB_STARTED msos20=%u bos=%u winusb_ifaces=dfu+vendor\n",
                (unsigned)device.microsoftOs20DescriptorLength(),
                (unsigned)device.bosDescriptorLength());

  // Long enough to look at the host side before it goes away.
  delay(20000);

#if S3_CABLE_ACTION == 1
  Serial.println("S3CABLE_REBOOT_ROM_DFU");
  Serial.flush();
  if (!device.rebootToRomDfu())
  {
    // Expected on a board whose USB_PHY_SEL eFuse is not burned. The device
    // must still be there on the host after this line.
    Serial.printf("S3CABLE_ROM_DFU_UNSUPPORTED %s\n", device.lastErrorName());
    return;
  }
#else
  Serial.println("S3CABLE_REBOOT_BOOTLOADER");
  Serial.flush();
  device.rebootToBootloader();
#endif
  Serial.println("S3CABLE_RETURNED_UNEXPECTEDLY");
}

void loop()
{
  delay(1000);
}
