#include <Arduino.h>
#include "EspUsbDevice.h"

// Composite device: HID keyboard + MSC on one EspUsbDevice.
// Suspected EP-allocation collision case (HID uses a private endpoint
// counter, MSC draws from the core allocator). Pairs with
// composite_hid_msc.ino (host). See tests/TEST_PLAN.ja.md.

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceMsc MSC(device);

static bool beginOk = false;
static const char *beginError = "ESP_OK";

static constexpr uint32_t BLOCK_COUNT = 16;
static constexpr uint16_t BLOCK_SIZE = 512;
static uint8_t disk[BLOCK_COUNT][BLOCK_SIZE];

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
  if (lba >= BLOCK_COUNT || offset >= BLOCK_SIZE)
  {
    return -1;
  }
  uint8_t *out = static_cast<uint8_t *>(buffer);
  uint32_t remaining = bufsize;
  uint32_t currentLba = lba;
  uint32_t currentOffset = offset;
  while (remaining > 0)
  {
    if (currentLba >= BLOCK_COUNT)
    {
      return -1;
    }
    const uint32_t chunk = min<uint32_t>(remaining, BLOCK_SIZE - currentOffset);
    memcpy(out, disk[currentLba] + currentOffset, chunk);
    out += chunk;
    remaining -= chunk;
    currentLba++;
    currentOffset = 0;
  }
  return bufsize;
}

// EspUsbDeviceMsc::begin() requires both a read and a write callback, so this
// must be set even for a capacity-only test.
static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
  if (lba >= BLOCK_COUNT || offset >= BLOCK_SIZE)
  {
    return -1;
  }
  uint8_t *in = buffer;
  uint32_t remaining = bufsize;
  uint32_t currentLba = lba;
  uint32_t currentOffset = offset;
  while (remaining > 0)
  {
    if (currentLba >= BLOCK_COUNT)
    {
      return -1;
    }
    const uint32_t chunk = min<uint32_t>(remaining, BLOCK_SIZE - currentOffset);
    memcpy(disk[currentLba] + currentOffset, in, chunk);
    in += chunk;
    remaining -= chunk;
    currentLba++;
    currentOffset = 0;
  }
  return bufsize;
}

static bool tapKeyWithRetry(char c)
{
  const uint32_t start = millis();
  while (millis() - start < 1000)
  {
    if (keyboard.tapKey(c))
    {
      return true;
    }
    delay(5);
  }
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  memset(disk, 0, sizeof(disk));
  disk[0][0] = 0xeb;
  disk[0][1] = 0x3c;
  disk[0][510] = 0x55;
  disk[0][511] = 0xaa;

  MSC.vendorID("ESP32");
  MSC.productID("MSC_HID");
  MSC.productRevision("1.0");
  MSC.onRead(onRead);
  MSC.onWrite(onWrite);
  MSC.mediaPresent(true);
  MSC.isWritable(true);
  MSC.begin(BLOCK_COUNT, BLOCK_SIZE);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4021;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice HID+MSC";
  config.serialNumber = "espusb-hid-msc";

  beginOk = device.begin(config);
  beginError = device.lastErrorName();
  Serial.printf("DEVICE_BEGIN %s %s\n", beginOk ? "ok" : "ng", beginError);
}

void loop()
{
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    if (command == '?')
    {
      Serial.println("DEVICE_READY");
    }
    else if (command == 'b')
    {
      Serial.printf("DEVICE_BEGIN %s %s\n", beginOk ? "ok" : "ng", beginError);
    }
    else if (command == 'k')
    {
      Serial.printf("DEVICE_KEY %u\n", tapKeyWithRetry('a') ? 1 : 0);
    }
  }
  delay(1);
}
