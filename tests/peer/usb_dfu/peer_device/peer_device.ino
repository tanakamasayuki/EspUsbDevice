// Peer device for the DFU test: an ordinary HID keyboard with an
// EspUsbDeviceDfu function beside it.
//
// The keyboard is there on purpose. DFU costs no endpoints, and the claim worth
// testing is that adding it to a device that already has a function changes
// nothing about that function - so the host sees a keyboard with its two
// endpoints plus a DFU interface with none.
//
// restartWhenComplete() is off. The test never sends a valid image, but a
// download that did succeed would reboot this board into it and take the test
// sketch with it.
#include "EspUsbDevice.h"
#include "esp_ota_ops.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware");

static volatile uint32_t blockCount = 0;
static volatile uint32_t byteCount = 0;
static volatile uint32_t errorCount = 0;
static volatile uint32_t completeCount = 0;
static volatile uint8_t lastStatus = 0;

static const char *bootPartitionLabel()
{
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  return boot ? boot->label : "none";
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  dfu.restartWhenComplete(false);
  dfu.onProgress([](size_t written)
                 {
                   blockCount++;
                   byteCount = written;
                 });
  dfu.onError([](uint8_t status)
              {
                errorCount++;
                lastStatus = status;
              });
  dfu.onComplete([]() -> bool
                 {
                   completeCount++;
                   // Never let a test image become the boot partition, whatever
                   // it contains. cancelPendingBoot() runs on the refusal path.
                   return false;
                 });

  if (!device.begin())
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf("DEVICE_READY %u\n", device.ready() ? 1 : 0);
    }
    else if (command == 's')
    {
      Serial.printf("DEVICE_DFU blocks=%u bytes=%u errors=%u complete=%u status=%u\n",
                    static_cast<unsigned>(blockCount),
                    static_cast<unsigned>(byteCount),
                    static_cast<unsigned>(errorCount),
                    static_cast<unsigned>(completeCount),
                    static_cast<unsigned>(lastStatus));
    }
    else if (command == 'b')
    {
      // The assertion that matters most: whatever the host did, this board still
      // boots the partition it is running from.
      const esp_partition_t *running = esp_ota_get_running_partition();
      Serial.printf("DEVICE_BOOT boot=%s running=%s\n",
                    bootPartitionLabel(),
                    running ? running->label : "none");
    }
    else if (command == 'z')
    {
      blockCount = 0;
      byteCount = 0;
      errorCount = 0;
      completeCount = 0;
      lastStatus = 0;
      Serial.println("DEVICE_RESET");
    }
    else if (command == 't')
    {
      Serial.printf("DEVICE_XFER %u\n", static_cast<unsigned>(EspUsbDeviceDfu::transferSize()));
    }
  }
  delay(5);
}
