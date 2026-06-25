#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceMsc msc(device);

static constexpr uint32_t BLOCK_COUNT = 16;
static constexpr uint16_t BLOCK_SIZE = 512;
static uint8_t storage[BLOCK_COUNT * BLOCK_SIZE];
EspUsbDeviceMscRamDisk disk(storage, BLOCK_COUNT, BLOCK_SIZE);

static void prepareDisk()
{
  disk.clear();

  // This is enough for low-level SCSI/block access tests. It is not a FAT image.
  disk.writeByte(0, 510, 0x55);
  disk.writeByte(0, 511, 0xaa);
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  prepareDisk();

  msc.vendorID("ESP32");
  msc.productID("MSC RAM DISK");
  msc.productRevision("1.0");
  msc.mediaPresent(true);
  msc.isWritable(true);
  msc.onStartStop([](uint8_t powerCondition, bool start, bool loadEject)
                  {
                    Serial.printf("MSC_START_STOP pc=%u start=%u eject=%u\n",
                                  powerCondition,
                                  start ? 1 : 0,
                                  loadEject ? 1 : 0);
                    return true;
                  });

  if (!disk.attach(msc))
  {
    Serial.println("MSC_ATTACH_FAILED");
    return;
  }

  EspUsbDeviceConfig config;
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4007;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice MSC";
  config.serialNumber = "espusb-msc";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.printf("USB MSC RAM disk ready blocks=%lu block_size=%u bytes=%u\n",
                static_cast<unsigned long>(disk.blockCount()),
                disk.blockSize(),
                static_cast<unsigned>(disk.byteSize()));
}

void loop()
{
  delay(1000);
}
