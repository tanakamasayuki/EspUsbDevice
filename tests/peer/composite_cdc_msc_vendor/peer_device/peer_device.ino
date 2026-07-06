#include <Arduino.h>
#include "EspUsbDevice.h"

// Non-HID triple: CDC + MSC + bulk Vendor. Covers the Vendor class in a
// composite without HID (so espUsbDeviceLoadHidDescriptor is never invoked and
// the HID+bulk-Vendor descriptor-duplication issue does not apply). All three
// draw endpoints from the core allocator, so this also exercises consistent
// dynamic numbering. Pairs with composite_cdc_msc_vendor.ino (host).

EspUsbDevice device;
EspUsbDeviceCdcSerial UsbSerial(device);
EspUsbDeviceMsc MSC(device);
EspUsbDeviceVendor Vendor(device);

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
  uint32_t remaining = bufsize, cur = lba, off = offset;
  while (remaining > 0)
  {
    if (cur >= BLOCK_COUNT)
    {
      return -1;
    }
    const uint32_t chunk = min<uint32_t>(remaining, BLOCK_SIZE - off);
    memcpy(out, disk[cur] + off, chunk);
    out += chunk;
    remaining -= chunk;
    cur++;
    off = 0;
  }
  return bufsize;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
  if (lba >= BLOCK_COUNT || offset >= BLOCK_SIZE)
  {
    return -1;
  }
  uint32_t remaining = bufsize, cur = lba, off = offset;
  while (remaining > 0)
  {
    if (cur >= BLOCK_COUNT)
    {
      return -1;
    }
    const uint32_t chunk = min<uint32_t>(remaining, BLOCK_SIZE - off);
    memcpy(disk[cur] + off, buffer, chunk);
    buffer += chunk;
    remaining -= chunk;
    cur++;
    off = 0;
  }
  return bufsize;
}

static void processVendorRx()
{
  size_t available = Vendor.available();
  uint8_t buffer[64];
  while (available > 0)
  {
    const size_t chunk = Vendor.read(buffer, min(available, sizeof(buffer)));
    if (chunk == 0)
    {
      break;
    }
    Vendor.write(reinterpret_cast<const uint8_t *>("echo:"), 5);
    Vendor.write(buffer, chunk);
    Vendor.flush();
    available = Vendor.available();
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  memset(disk, 0, sizeof(disk));
  disk[0][0] = 0xeb;
  disk[0][510] = 0x55;
  disk[0][511] = 0xaa;

  MSC.vendorID("ESP32");
  MSC.productID("MSC_TRIPLE");
  MSC.productRevision("1.0");
  MSC.onRead(onRead);
  MSC.onWrite(onWrite);
  MSC.mediaPresent(true);
  MSC.isWritable(true);
  MSC.begin(BLOCK_COUNT, BLOCK_SIZE);

  Vendor.onRx([](size_t)
              { processVendorRx(); });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4023;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice CDC+MSC+Vendor";
  config.serialNumber = "espusb-triple";

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
