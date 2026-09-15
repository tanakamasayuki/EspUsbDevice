#include "EspUsbDevice.h"

// Entering the ROM download loader ("boot mode") from a running sketch, so the
// whole flash can be rewritten over USB without anyone pressing BOOT.
//
// Three ways in, all reaching the same place:
//
//   1. Open the CDC port at 1200 baud and drop DTR. This is the gesture the
//      Arduino IDE and arduino-cli already make, so it needs nothing new on the
//      host:   arduino-cli upload ...   or   stty -F /dev/ttyACM0 1200
//   2. dfu-util -e, through the DFU runtime interface. A standard host tool
//      asking a standard question; costs one interface and no endpoints.
//   3. Send 'b' on the CDC port, for a host script of your own.
//
// Then flash as usual:
//
//   esptool --port <port> write_flash 0x0 firmware.bin
//
// Which port the ROM comes back on differs per chip. On a one-connector
// ESP32-S3 the shared PHY returns to USB Serial/JTAG, so the same cable keeps
// working. On ESP32-P4 the loader answers on the USB Serial/JTAG port, which is
// not the high-speed OTG connector. See docs/ota-over-usb.md, section 2.3.
//
// This replaces the whole flash, so it always works - including on firmware
// with no OTA partition and firmware too broken to update itself. It is the
// recovery path behind examples/FirmwareDFU and examples/FirmwareHTTP, not a
// replacement for them.

EspUsbDevice device;
EspUsbDeviceCdcSerial port(device, "Console");
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime, "Bootloader");

// Set from USB callbacks, acted on in loop(). Every class callback in this
// library runs on the usbd task, and restarting the chip from inside one cuts
// off the transfer the host is still finishing.
static volatile bool bootloaderRequested = false;

void setup()
{
  Serial.begin(115200);
  delay(1500);

  // The 1200-baud touch. A host that merely opens the port at some other speed
  // is not asking for anything, so nothing happens.
  port.onLineCoding([](const EspUsbDeviceCdcLineCoding &coding)
                    {
                      if (coding.baud == 1200)
                      {
                        bootloaderRequested = true;
                      }
                    });

  port.onRx([](size_t)
            {
              while (port.available() > 0)
              {
                if (port.read() == 'b')
                {
                  bootloaderRequested = true;
                }
              }
            });

  // Without a callback, EspUsbDeviceDfu in Runtime mode restarts into the
  // loader by itself, which is what a host that sent DFU_DETACH asked for.
  // Taking the callback here only adds the log line - and shows where a sketch
  // would refuse, or save state first.
  dfu.onDetach([]()
               {
                 Serial.println("DFU_DETACH received");
                 bootloaderRequested = true;
               });

  if (!device.begin())
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Ready. Touch the CDC port at 1200 baud, run dfu-util -e, or send 'b'.");
}

void loop()
{
  if (bootloaderRequested)
  {
    bootloaderRequested = false;
    Serial.println("Restarting into the ROM download loader");
    Serial.flush();
    // Detaches the USB device, sets the target's download-boot flag and
    // restarts. Does not return.
    device.rebootToBootloader();
    // Only reached on a target with no such register, which none of the chips
    // this library supports is.
    Serial.printf("REBOOT_UNSUPPORTED %s\n", device.lastErrorName());
  }
  delay(20);
}
