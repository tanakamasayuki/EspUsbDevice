// Does Windows bind WinUSB to a bare vendor interface, with no .inf file?
//
// This is the test the Microsoft OS 2.0 layout work exists for. A
// single-interface vendor device gets the compatible ID directly under the set
// header, because a function subset only resolves through usbccgp.sys and
// Windows loads that for composite devices only. The failure it replaces is
// CM_PROB_FAILED_INSTALL (code 28) with nothing at all in setupapi.dev.log.
//
// Read the result with windows_winusb.py, which asks Windows rather than the
// device: the device answering correctly is exactly what the old failure looked
// like from this side.
//
// USE A SERIAL NUMBER THAT HAS NEVER FAILED TO INSTALL ON THIS PC. Windows keys
// a device instance on VID, PID and serial, and a failed driver match sticks to
// that instance and is never re-probed - so re-testing under a serial that once
// failed reports the cached failure, not what the descriptors now say. Pass a
// new one per attempt:
//
//   echo '-DWINUSB_TEST_SERIAL=\"espusb-winusb-2\"' > build_opt.h
#include "EspUsbDevice.h"

#ifndef WINUSB_TEST_SERIAL
#define WINUSB_TEST_SERIAL "espusb-winusb-1"
#endif

// 0 = AUTO (flat here, since this device has one interface), 2 = force the
// configuration/function subsets. Forcing them is the control: it is the shape
// the library used to emit unconditionally, and on a single-interface device it
// is what Windows cannot resolve.
#ifndef WINUSB_TEST_LAYOUT
#define WINUSB_TEST_LAYOUT 0
#endif

EspUsbDevice device;
EspUsbDeviceVendor vendor(device, 512);

void setup()
{
  Serial.begin(115200);
  delay(1500);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4043;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice WinUSB Check";
  config.serialNumber = WINUSB_TEST_SERIAL;
  config.controller = EspUsbController::HighSpeed;
  // What makes the library serve the BOS and the Microsoft OS 2.0 descriptor
  // set whose WINUSB compatible ID binds the driver.
  config.webusbEnabled = true;
  config.webusbUrl = "https://example.com/espusbdevice";
  config.msOs20Layout = static_cast<EspUsbDeviceMsOs20Layout>(WINUSB_TEST_LAYOUT);

  if (!device.begin(config))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("WINUSB_CHECK serial=%s interfaces=%u ms_os_20_len=%u subsets=%u\n",
                WINUSB_TEST_SERIAL,
                device.configurationDescriptor(0)[4],
                device.microsoftOs20DescriptorLength(),
                device.microsoftOs20UsesSubsets() ? 1U : 0U);
}

void loop()
{
  delay(1000);
}
