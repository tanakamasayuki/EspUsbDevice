#include "../../../sketch_support/UsbTestLifecycle.h"
#include <Arduino.h>
#include "EspUsbDevice.h"

// Max composite that fits the S3 endpoint budget: HID keyboard + CDC + MSC.
// The data endpoints use duplex numbers, but the three data IN endpoints plus
// CDC notification consume all usable S3 IN endpoints. Adding a fourth class
// that needs IN fails at SET_CONFIGURATION. Pairs with
// composite_hid_cdc_msc.ino (host).

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceCdcSerial UsbSerial(device);
EspUsbDeviceMsc MSC(device);

static bool beginOk = false;
static const char *beginError = "ESP_OK";

static constexpr uint32_t BLOCK_COUNT = 16;
static constexpr uint16_t BLOCK_SIZE = 512;
static uint8_t disk[BLOCK_COUNT * BLOCK_SIZE];

EspUsbDeviceMscRamDisk ramDisk(disk, BLOCK_COUNT, BLOCK_SIZE);

void setup()
{
  Serial.begin(115200);
  delay(500);

  memset(disk, 0, sizeof(disk));
  disk[0] = 0xeb;
  disk[510] = 0x55;
  disk[511] = 0xaa;

  MSC.vendorID("ESP32");
  MSC.productID("MSC_QUAD");
  MSC.productRevision("1.0");
  MSC.mediaPresent(true);
  MSC.isWritable(true);
  if (!ramDisk.attach(MSC))
  {
    beginError = "MSC_ATTACH_FAILED";
    return;
  }

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4022;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice HID+CDC+MSC";
  config.serialNumber = "espusb-hid-cdc-msc";

  beginOk = device.begin(config);
  beginError = device.lastErrorName();
  Serial.printf("DEVICE_BEGIN %s %s\n", beginOk ? "ok" : "ng", beginError);
}

// Block until the host has configured us, so every command below answers from a
// usable device whatever order the tests run in.
//
// device.ready() is tud_mounted(): the host completed SET_CONFIGURATION. That is
// the event the tests actually depend on, and it is answered on demand rather
// than announced once at boot - a test that does not run first never sees an
// announcement, which is what made most of this suite fail when its modules were
// run in reverse. tests/peer/usb_msc has had this shape all along and was the
// only peer module that survived that check.
static bool waitForHost(uint32_t timeoutMs = 5000)
{
  const uint32_t startedAt = millis();
  while (!device.ready() && millis() - startedAt < timeoutMs)
  {
    device.task();
    delay(10);
  }
  return device.ready();
}

void loop()
{
  if (handleUsbTestStop([] { device.end(); }, "composite_hid_cdc_msc"))
  {
    return;
  }
  if (Serial.available() > 0 && !usbTestControlPending())
  {
    char command = Serial.read();
    const bool hostReady = waitForHost();
    if (command == '?')
    {
      Serial.printf("DEVICE_READY %u\n", hostReady ? 1 : 0);
    }
    else if (command == 'b')
    {
      Serial.printf("DEVICE_BEGIN %s %s\n", beginOk ? "ok" : "ng", beginError);
    }
    else if (command == 'k')
    {
      Serial.printf("DEVICE_KEY %u\n", keyboard.tapKey('a') ? 1 : 0);
    }
    else if (command == 'd')
    {
      const uint8_t payload[] = "device to host";
      Serial.printf("DEVICE_TX %u\n", UsbSerial.write(payload, sizeof(payload) - 1) == sizeof(payload) - 1 ? 1 : 0);
    }
  }

  while (UsbSerial.available() > 0)
  {
    Serial.print("DEVICE_RX ");
    while (UsbSerial.available() > 0)
    {
      Serial.write(UsbSerial.read());
    }
    Serial.println();
  }
  delay(1);
}
