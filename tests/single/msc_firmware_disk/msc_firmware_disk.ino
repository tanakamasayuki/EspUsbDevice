// EspUsbDeviceMscFirmwareDisk without a USB host.
//
// The class's contract is with whatever calls its block callbacks, so the test
// calls them itself: that covers the geometry, the FAT it publishes, the image
// detection, the sequential-order rule, and the commit - on one board, with the
// serial log alive because the USB stack is never started.
#include "EspUsbDevice.h"
#include "esp_ota_ops.h"

static int passCount = 0;
static int failCount = 0;

static void check(bool condition, const char *name)
{
  if (condition)
  {
    passCount++;
  }
  else
  {
    Serial.print("FAIL ");
    Serial.println(name);
    failCount++;
  }
}

static uint16_t le16(const uint8_t *data)
{
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

static uint32_t le32(const uint8_t *data)
{
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

static uint8_t storage[16 * 1024];
static uint8_t sector[512];

// A block that looks like the start of an ESP application: the magic byte is
// what esp_ota_write() checks before it accepts anything at all. The rest is
// deliberately nonsense, so the image fails verification at commit and the boot
// partition never moves.
static void fillImageBlock(uint8_t *data, size_t length, bool magic, uint8_t seed)
{
  for (size_t i = 0; i < length; i++)
  {
    data[i] = static_cast<uint8_t>(seed + i);
  }
  data[0] = magic ? 0xe9 : 0x00;
}

static const char *bootLabel()
{
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  return boot ? boot->label : "none";
}

static void testGeometry()
{
  EspUsbDeviceMscFirmwareDisk disk(storage, sizeof(storage));
  check(disk.begin("ESPUSB"), "geometry_begin");
  check(disk.blockSize() == 512, "geometry_block_size");

  // The volume has to be able to hold one image of the full partition size,
  // plus the scratch area, plus the metadata.
  const size_t capacity = EspUsbDeviceFirmwareUpdate::capacity();
  check(capacity > 0, "geometry_capacity");
  check(static_cast<size_t>(disk.blockCount()) * 512 >= capacity, "geometry_holds_partition");
  // Everything in RAM must fit the buffer the sketch supplied.
  check(static_cast<size_t>(disk.ramSectorCount()) * 512 <= sizeof(storage), "geometry_ram_fits");
  check(disk.ramSectorCount() < disk.blockCount(), "geometry_has_firmware_region");
  check(!disk.updating(), "geometry_idle");

  check(disk.read(0, 0, sector, 512) == 512, "geometry_read_boot");
  check(sector[510] == 0x55 && sector[511] == 0xaa, "boot_signature");
  check(le16(&sector[11]) == 512, "boot_bytes_per_sector");
  check(sector[13] >= 8, "boot_sectors_per_cluster");
  check(sector[16] == 2, "boot_fat_count");
  check(memcmp(&sector[54], "FAT12   ", 8) == 0, "boot_fat12");
  const uint32_t declared = le16(&sector[19]) ? le16(&sector[19]) : le32(&sector[32]);
  check(declared == disk.blockCount(), "boot_total_sectors");

  // The FAT starts empty apart from the two reserved entries: every cluster
  // that maps onto the partition has to look free, or the host will not put a
  // file there.
  check(disk.read(1, 0, sector, 512) == 512, "geometry_read_fat");
  check(sector[0] == 0xf8 && sector[1] == 0xff && sector[2] == 0xff, "fat_reserved_entries");
  check(sector[3] == 0x00 && sector[4] == 0x00, "fat_rest_is_free");

  // A sector inside the firmware region reads back from the partition rather
  // than as zeros, which is what lets a host verify what it copied.
  check(disk.read(disk.ramSectorCount(), 0, sector, 512) == 512, "geometry_read_firmware_region");
}

static void testReadmeFile()
{
  EspUsbDeviceMscFirmwareDisk disk(storage, sizeof(storage));
  check(disk.begin("ESPUSB"), "readme_begin");
  check(disk.addTextFile("README.TXT", "Drop firmware.bin here.\r\n"), "readme_add");
  check(!disk.addTextFile("TOOLONGNAME.TXT", "x"), "readme_rejects_long_name");

  // The root directory sits right after both FAT copies.
  const uint8_t sectorsPerFat = 1;
  (void)sectorsPerFat;
  uint8_t boot[512];
  check(disk.read(0, 0, boot, 512) == 512, "readme_read_boot");
  const uint32_t rootSector = 1 + (2u * le16(&boot[22]));
  check(disk.read(rootSector, 0, sector, 512) == 512, "readme_read_root");
  check(memcmp(sector, "README  TXT", 11) == 0, "readme_entry_name");
  check(le16(&sector[26]) == 2, "readme_first_cluster");
  check(le32(&sector[28]) == 25, "readme_size");
}

// A write that is not an ESP image must be dropped, not flashed: it is the
// host's metadata spilling out of the scratch area, or a file dropped by
// mistake.
static void testNonImageIgnored()
{
  EspUsbDeviceMscFirmwareDisk disk(storage, sizeof(storage));
  check(disk.begin("ESPUSB"), "ignore_begin");

  uint8_t block[512];
  fillImageBlock(block, sizeof(block), false, 0x10);
  check(disk.write(disk.ramSectorCount(), 0, block, sizeof(block)) == 512, "ignore_write_accepted");
  check(!disk.updating(), "ignore_no_update_started");
  check(disk.written() == 0, "ignore_nothing_written");
}

// Ascending order is the contract. A write that jumps backwards or leaves a
// hole is refused and the update abandoned, rather than producing an image that
// looks written and is not.
static void testOutOfOrderRefused()
{
  EspUsbDeviceMscFirmwareDisk disk(storage, sizeof(storage));
  check(disk.begin("ESPUSB"), "order_begin");

  static volatile int errors = 0;
  disk.onError([](esp_err_t error)
               {
                 (void)error;
                 errors++;
               });

  uint8_t block[512];
  fillImageBlock(block, sizeof(block), true, 0x20);
  const uint32_t start = disk.ramSectorCount();
  check(disk.write(start, 0, block, sizeof(block)) == 512, "order_first_block");
  check(disk.updating(), "order_update_started");
  check(disk.written() == 512, "order_first_block_counted");

  // Two sectors on: a hole.
  fillImageBlock(block, sizeof(block), false, 0x30);
  check(disk.write(start + 2, 0, block, sizeof(block)) == -1, "order_gap_refused");
  check(!disk.updating(), "order_update_abandoned");
  check(errors == 1, "order_error_reported");

  check(strcmp(bootLabel(), esp_ota_get_running_partition()->label) == 0, "order_boot_unchanged");
}

// The whole path: image detected, blocks streamed, the directory entry the host
// writes says how long the file is, and the commit refuses it because the bytes
// are nonsense. Exactly the shape of a real update, minus a real image.
static void testStreamAndVerify()
{
  EspUsbDeviceMscFirmwareDisk disk(storage, sizeof(storage));
  check(disk.begin("ESPUSB"), "stream_begin");

  static volatile int errors = 0;
  static volatile int completes = 0;
  static volatile size_t lastProgress = 0;
  disk.onError([](esp_err_t error)
               {
                 (void)error;
                 errors++;
               });
  disk.onComplete([]() -> bool
                  {
                    completes++;
                    return false; // never let a test image boot
                  });
  disk.onProgress([](size_t written) { lastProgress = written; });
  // Nothing in this test may restart the board.
  disk.restartWhenComplete(false);

  uint8_t boot[512];
  check(disk.read(0, 0, boot, 512) == 512, "stream_read_boot");
  const uint32_t sectorsPerCluster = boot[13];
  const uint32_t rootSector = 1 + (2u * le16(&boot[22]));
  const uint32_t start = disk.ramSectorCount();
  const uint32_t firstFirmwareCluster = (start - (rootSector + 1)) / sectorsPerCluster + 2;

  uint8_t block[512];
  fillImageBlock(block, sizeof(block), true, 0x40);
  check(disk.write(start, 0, block, sizeof(block)) == 512, "stream_block_0");
  fillImageBlock(block, sizeof(block), false, 0x50);
  check(disk.write(start + 1, 0, block, sizeof(block)) == 512, "stream_block_1");
  check(disk.written() == 1024, "stream_two_blocks");
  check(lastProgress == 1024, "stream_progress");
  check(completes == 0, "stream_not_complete_yet");

  // Now the directory entry, as a host writes it: name, first cluster inside
  // the firmware region, and the length.
  check(disk.read(rootSector, 0, sector, 512) == 512, "stream_read_root");
  memcpy(sector, "FIRMWAREBIN", 11);
  sector[11] = 0x20;
  sector[26] = static_cast<uint8_t>(firstFirmwareCluster & 0xff);
  sector[27] = static_cast<uint8_t>((firstFirmwareCluster >> 8) & 0xff);
  sector[28] = 0x00;
  sector[29] = 0x04; // 1024 bytes
  sector[30] = 0x00;
  sector[31] = 0x00;
  check(disk.write(rootSector, 0, sector, 512) == 512, "stream_write_root");

  // The byte count has reached the announced length, so the disk committed -
  // and the commit failed verification, because the image is nonsense.
  check(!disk.updating(), "stream_update_closed");
  check(completes == 0, "stream_complete_not_called_for_bad_image");
  check(errors == 1, "stream_verification_error_reported");
  check(strcmp(bootLabel(), esp_ota_get_running_partition()->label) == 0, "stream_boot_unchanged");
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("TEST_BEGIN msc_firmware_disk");
  Serial.print("BOOT_BEFORE ");
  Serial.println(bootLabel());

  testGeometry();
  testReadmeFile();
  testNonImageIgnored();
  testOutOfOrderRefused();
  testStreamAndVerify();

  Serial.print("BOOT_AFTER ");
  Serial.println(bootLabel());
  Serial.print("PASS ");
  Serial.print(passCount);
  Serial.print(" FAIL ");
  Serial.println(failCount);
  Serial.println("TEST_END");
  Serial.println(failCount == 0 ? "OK" : "NG");
}

void loop()
{
  delay(1000);
}
