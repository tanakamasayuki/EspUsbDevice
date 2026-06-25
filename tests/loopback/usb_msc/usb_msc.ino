#include "EspUsbDevice.h"
#include "EspUsbHost.h"
#include <string.h>

EspUsbDevice device;
EspUsbDeviceMsc MSC(device);
EspUsbHost usb;

static constexpr uint32_t BLOCK_COUNT = 16;
static constexpr uint16_t BLOCK_SIZE = 512;
static uint8_t disk[BLOCK_COUNT][BLOCK_SIZE];
static volatile bool deviceConnected = false;
static volatile bool failNextWrite = false;

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

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
  if (failNextWrite)
  {
    failNextWrite = false;
    Serial.printf("DEVICE_WRITE_FAIL lba=%lu offset=%lu size=%lu\n",
                  static_cast<unsigned long>(lba),
                  static_cast<unsigned long>(offset),
                  static_cast<unsigned long>(bufsize));
    return -1;
  }
  if (lba >= BLOCK_COUNT || offset >= BLOCK_SIZE)
  {
    return -1;
  }
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
    memcpy(disk[currentLba] + currentOffset, buffer, chunk);
    Serial.printf("DEVICE_WRITE lba=%lu offset=%lu size=%lu\n",
                  static_cast<unsigned long>(currentLba),
                  static_cast<unsigned long>(currentOffset),
                  static_cast<unsigned long>(chunk));
    buffer += chunk;
    remaining -= chunk;
    currentLba++;
    currentOffset = 0;
  }
  return bufsize;
}

static bool onStartStop(uint8_t powerCondition, bool start, bool loadEject)
{
  (void)powerCondition;
  (void)start;
  (void)loadEject;
  return true;
}

static bool waitFor(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t started = millis();
  while (!flag && millis() - started < timeoutMs)
  {
    delay(10);
  }
  return flag;
}

static bool waitForMsc(uint32_t timeoutMs = 10000)
{
  const uint32_t started = millis();
  while (!usb.mscReady() && millis() - started < timeoutMs)
  {
    delay(10);
  }
  return usb.mscReady();
}

static bool checkBasic()
{
  bool ok = true;

  uint32_t blocks32 = 0;
  uint32_t blockSize32 = 0;
  bool commandOk = usb.mscCapacity(blocks32, blockSize32);
  Serial.printf("MSC_CAPACITY ok=%u blocks=%lu block_size=%lu\n",
                commandOk ? 1 : 0,
                static_cast<unsigned long>(blocks32),
                static_cast<unsigned long>(blockSize32));
  ok = ok && commandOk && blocks32 == BLOCK_COUNT && blockSize32 == BLOCK_SIZE;

  EspUsbHostMscBlockDeviceInfo info;
  commandOk = usb.mscGetBlockDeviceInfo(info);
  Serial.printf("MSC_BLOCK_DEVICE ok=%u addr=%u iface=%u lun=%u max_lun=%u blocks=%llu block_size=%lu bytes=%llu\n",
                commandOk ? 1 : 0,
                info.address,
                info.interfaceNumber,
                info.lun,
                info.maxLun,
                static_cast<unsigned long long>(info.blockCount),
                static_cast<unsigned long>(info.blockSize),
                static_cast<unsigned long long>(info.capacityBytes));
  ok = ok && commandOk && info.lun == 0 && info.maxLun == 0 && info.blockCount == BLOCK_COUNT && info.blockSize == BLOCK_SIZE;

  uint64_t blocks64 = 0;
  uint32_t blockSize64 = 0;
  commandOk = usb.mscCapacity64(blocks64, blockSize64);
  Serial.printf("MSC_CAPACITY64 ok=%u blocks=%lu block_size=%lu\n",
                commandOk ? 1 : 0,
                static_cast<unsigned long>(blocks64),
                static_cast<unsigned long>(blockSize64));
  ok = ok && commandOk && blocks64 == BLOCK_COUNT && blockSize64 == BLOCK_SIZE;

  EspUsbHostMscInquiry inquiry;
  commandOk = usb.mscInquiry(inquiry);
  Serial.printf("MSC_INQUIRY ok=%u removable=%u vendor='%s' product='%s' revision='%s'\n",
                commandOk ? 1 : 0,
                inquiry.removable ? 1 : 0,
                inquiry.vendor,
                inquiry.product,
                inquiry.revision);
  ok = ok && commandOk && inquiry.removable && strcmp(inquiry.vendor, "ESP32") == 0 && strcmp(inquiry.product, "MSC_PEER") == 0 && strcmp(inquiry.revision, "1.0") == 0;

  uint8_t maxLun = 0xff;
  commandOk = usb.mscMaxLun(maxLun);
  Serial.printf("MSC_MAX_LUN ok=%u max_lun=%u\n", commandOk ? 1 : 0, maxLun);
  ok = ok && commandOk && maxLun == 0;

  commandOk = usb.mscSelectLun(0);
  Serial.printf("MSC_SELECT_LUN ok=%u\n", commandOk ? 1 : 0);
  ok = ok && commandOk;

  EspUsbHostMscSense sense;
  commandOk = usb.mscRequestSense(sense);
  Serial.printf("MSC_SENSE ok=%u response=0x%02x key=0x%02x asc=0x%02x ascq=0x%02x\n",
                commandOk ? 1 : 0,
                sense.responseCode,
                sense.senseKey,
                sense.additionalSenseCode,
                sense.additionalSenseQualifier);
  ok = ok && commandOk && sense.responseCode == 0x70 && sense.senseKey == 0x00 && sense.additionalSenseCode == 0x00 && sense.additionalSenseQualifier == 0x00;

  commandOk = usb.mscLastSense(sense);
  Serial.printf("MSC_LAST_SENSE ok=%u response=0x%02x key=0x%02x asc=0x%02x ascq=0x%02x\n",
                commandOk ? 1 : 0,
                sense.responseCode,
                sense.senseKey,
                sense.additionalSenseCode,
                sense.additionalSenseQualifier);
  ok = ok && commandOk && sense.responseCode == 0x70 && sense.senseKey == 0x00 && sense.additionalSenseCode == 0x00 && sense.additionalSenseQualifier == 0x00;

  commandOk = usb.mscTestUnitReady();
  Serial.printf("MSC_TEST_UNIT_READY ok=%u\n", commandOk ? 1 : 0);
  ok = ok && commandOk;

  commandOk = usb.mscWaitReady(ESP_USB_HOST_ANY_ADDRESS, 1000, 1000);
  Serial.printf("MSC_WAIT_READY ok=%u\n", commandOk ? 1 : 0);
  ok = ok && commandOk;

  commandOk = usb.mscSynchronizeCache();
  Serial.printf("MSC_SYNC_CACHE ok=%u\n", commandOk ? 1 : 0);
  ok = ok && commandOk;

  return ok;
}

static bool checkReads()
{
  bool ok = true;
  uint8_t block[512] = {};

  bool commandOk = usb.mscReadBlocks(0, block, 1);
  Serial.printf("MSC_READ ok=%u b0=%02x b1=%02x b510=%02x b511=%02x\n",
                commandOk ? 1 : 0,
                block[0],
                block[1],
                block[510],
                block[511]);
  ok = ok && commandOk && block[0] == 0xeb && block[1] == 0x3c && block[510] == 0x55 && block[511] == 0xaa;

  memset(block, 0, sizeof(block));
  commandOk = usb.mscReadBlocks64(0, block, 1);
  Serial.printf("MSC_READ64 ok=%u b0=%02x b1=%02x b510=%02x b511=%02x\n",
                commandOk ? 1 : 0,
                block[0],
                block[1],
                block[510],
                block[511]);
  ok = ok && commandOk && block[0] == 0xeb && block[1] == 0x3c && block[510] == 0x55 && block[511] == 0xaa;

  return ok;
}

static bool checkWrites()
{
  bool ok = true;
  uint8_t block[512] = {};

  for (size_t i = 0; i < sizeof(block); i++)
  {
    block[i] = static_cast<uint8_t>(i ^ 0xa5);
  }
  bool writeOk = usb.mscWriteBlocks(4, block, 1);
  memset(block, 0, sizeof(block));
  bool readOk = usb.mscReadBlocks(4, block, 1);
  Serial.printf("MSC_WRITE_READ write=%u read=%u b0=%02x b1=%02x b255=%02x b511=%02x\n",
                writeOk ? 1 : 0,
                readOk ? 1 : 0,
                block[0],
                block[1],
                block[255],
                block[511]);
  ok = ok && writeOk && readOk && block[0] == 0xa5 && block[1] == 0xa4 && block[255] == 0x5a && block[511] == 0x5a;

  for (size_t i = 0; i < sizeof(block); i++)
  {
    block[i] = static_cast<uint8_t>(0x5a ^ i);
  }
  writeOk = usb.mscWriteBlocks64(5, block, 1);
  memset(block, 0, sizeof(block));
  readOk = usb.mscReadBlocks64(5, block, 1);
  Serial.printf("MSC_WRITE_READ64 write=%u read=%u b0=%02x b1=%02x b255=%02x b511=%02x\n",
                writeOk ? 1 : 0,
                readOk ? 1 : 0,
                block[0],
                block[1],
                block[255],
                block[511]);
  ok = ok && writeOk && readOk && block[0] == 0x5a && block[1] == 0x5b && block[255] == 0xa5 && block[511] == 0xa5;

  uint8_t twoBlocks[1024] = {};
  for (size_t i = 0; i < sizeof(twoBlocks); i++)
  {
    twoBlocks[i] = static_cast<uint8_t>((i + 0x31) & 0xff);
  }
  writeOk = usb.mscWriteBlocks(6, twoBlocks, 2);
  memset(twoBlocks, 0, sizeof(twoBlocks));
  readOk = usb.mscReadBlocks(6, twoBlocks, 2);
  Serial.printf("MSC_MULTI write=%u read=%u b0=%02x b511=%02x b512=%02x b1023=%02x\n",
                writeOk ? 1 : 0,
                readOk ? 1 : 0,
                twoBlocks[0],
                twoBlocks[511],
                twoBlocks[512],
                twoBlocks[1023]);
  ok = ok && writeOk && readOk && twoBlocks[0] == 0x31 && twoBlocks[511] == 0x30 && twoBlocks[512] == 0x31 && twoBlocks[1023] == 0x30;

  static uint8_t chunked[512 * 9] = {};
  for (size_t i = 0; i < sizeof(chunked); i++)
  {
    chunked[i] = static_cast<uint8_t>((i * 3 + 0x17) & 0xff);
  }
  writeOk = usb.mscWriteBlocks(1, chunked, 9);
  memset(chunked, 0, sizeof(chunked));
  readOk = usb.mscReadBlocks(1, chunked, 9);
  Serial.printf("MSC_CHUNKED write=%u read=%u b0=%02x b4095=%02x b4096=%02x b4607=%02x\n",
                writeOk ? 1 : 0,
                readOk ? 1 : 0,
                chunked[0],
                chunked[4095],
                chunked[4096],
                chunked[4607]);
  ok = ok && writeOk && readOk && chunked[0] == 0x17 && chunked[4095] == 0x14 && chunked[4096] == 0x17 && chunked[4607] == 0x14;

  return ok;
}

static bool checkErrors()
{
  bool ok = true;
  uint8_t block[512] = {};

  const bool readOk = usb.mscReadBlocks(16, block, 1);
  const bool writeOk = usb.mscWriteBlocks(16, block, 1);
  Serial.printf("MSC_OUT_OF_RANGE read=%u write=%u\n", readOk ? 1 : 0, writeOk ? 1 : 0);
  ok = ok && !readOk && !writeOk;

  for (size_t i = 0; i < sizeof(block); i++)
  {
    block[i] = static_cast<uint8_t>(0xe0 | (i & 0x0f));
  }
  failNextWrite = true;
  Serial.println("DEVICE_FAIL_NEXT_WRITE armed=1");
  const bool failedWriteOk = usb.mscWriteBlocks(10, block, 1);
  Serial.printf("MSC_FAILED_WRITE write=%u\n", failedWriteOk ? 1 : 0);
  ok = ok && !failedWriteOk;

  return ok;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_usb_msc");

  memset(disk, 0, sizeof(disk));
  disk[0][0] = 0xeb;
  disk[0][1] = 0x3c;
  disk[0][510] = 0x55;
  disk[0][511] = 0xaa;

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  EspUsbHostConfig hostConfig;
  hostConfig.port = ESP_USB_HOST_PORT_FULL_SPEED;
  if (!usb.begin(hostConfig))
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("HOST_READY fs");

  MSC.vendorID("ESP32");
  MSC.productID("MSC_PEER");
  MSC.productRevision("1.0");
  MSC.onStartStop(onStartStop);
  MSC.onRead(onRead);
  MSC.onWrite(onWrite);
  MSC.mediaPresent(true);
  MSC.isWritable(true);
  MSC.begin(BLOCK_COUNT, BLOCK_SIZE);

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  deviceConfig.speed = ESP_USB_DEVICE_SPEED_FULL;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4018;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback MSC";
  deviceConfig.serialNumber = "espusb-loopback-msc";

  if (!device.begin(deviceConfig))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("DEVICE_READY fs");

  bool ok = true;
  if (!waitFor(deviceConnected, 30000) || !waitForMsc())
  {
    Serial.printf("DEVICE_TIMEOUT host_error=%s device_error=%s\n", usb.lastErrorName(), device.lastErrorName());
    ok = false;
  }

  ok = ok && checkBasic();
  ok = ok && checkReads();
  ok = ok && checkWrites();
  ok = ok && checkErrors();

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
