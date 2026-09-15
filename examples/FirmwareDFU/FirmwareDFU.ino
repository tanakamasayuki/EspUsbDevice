#include "EspUsbDevice.h"

// Firmware update over USB DFU, with no ROM bootloader involved.
//
//   dfu-util -l                       # find the device
//   dfu-util -D FirmwareDFU.ino.bin   # write it, then the board restarts into it
//
// The device stays a working HID keyboard the whole time. DFU costs one
// interface and no endpoints - every transfer travels on EP0 - so it is the one
// function that can be added to a device whose endpoint budget is already spent.
// See docs/ota-over-usb.md for the other routes and for boot mode.
//
// Two things to know before running this:
//
//  - It needs two application partitions. The Arduino "Default" partition
//    scheme has them; "Huge APP" does not, and the sketch says so at startup
//    rather than failing halfway through an upload.
//  - The image you send replaces this sketch. Send a build of a sketch that
//    also has DFU in it, or the next update has to go through boot mode.

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware");

static volatile uint32_t reportedKiB = 0;

void setup()
{
  Serial.begin(115200);
  delay(1500);

  if (!EspUsbDeviceFirmwareUpdate::available())
  {
    // Nothing below can work under a single-app partition scheme. Say it once,
    // clearly, instead of letting a 300 KB upload fail on its last block.
    Serial.println("NO_OTA_PARTITION - select a partition scheme with two app partitions");
  }
  else
  {
    Serial.printf("Update target: %s (%u bytes)\n",
                  EspUsbDeviceFirmwareUpdate::targetLabel(),
                  static_cast<unsigned>(EspUsbDeviceFirmwareUpdate::capacity()));
  }

  // Progress runs on the usbd task, once per DFU block. Printing every block
  // would be the slowest thing in the transfer, so this reports each kilobyte.
  // There is no percentage to show: DFU 1.1 never tells the device how long the
  // image is.
  dfu.onProgress([](size_t written)
                 {
                   const uint32_t kiB = written / 1024;
                   if (kiB != reportedKiB)
                   {
                     reportedKiB = kiB;
                     Serial.printf("DFU %u KiB\n", static_cast<unsigned>(kiB));
                   }
                 });

  // The image is written and verified by the time this runs, and the boot
  // partition has already moved. Returning false would put it back.
  dfu.onComplete([]() -> bool
                 {
                   Serial.println("DFU complete - restarting into the new firmware");
                   return true;
                 });

  dfu.onError([](uint8_t status)
              {
                // DFU 1.1 status codes: 3 = errWRITE (the image was refused
                // before anything was written, usually a file that is not an
                // ESP application), 7 = errVERIFY (it did not survive
                // verification). dfu-util prints the same number.
                Serial.printf("DFU failed, status %u\n", static_cast<unsigned>(status));
              });

  if (!device.begin())
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Ready. dfu-util -D <firmware.bin>");
}

void loop()
{
  // The firmware that is running has proved it can enumerate, which is as good
  // a definition of "this image works" as a device like this has. Confirming it
  // cancels the rollback a bootloader built with
  // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE would otherwise perform at the next
  // restart. On a stock Arduino build there is no rollback pending and this
  // does nothing.
  static bool confirmed = false;
  if (!confirmed && device.ready())
  {
    confirmed = true;
    EspUsbDeviceFirmwareUpdate::markValid();
  }
  delay(20);
}
