#include "EspUsbDevice.h"

static int passCount = 0;
static int failCount = 0;
static uint8_t storage[64 * 1024];

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
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

static uint16_t fat12Entry(const uint8_t *fat, uint16_t cluster)
{
  const uint32_t offset = cluster + (cluster / 2);
  if (cluster & 1)
  {
    return static_cast<uint16_t>(((fat[offset] >> 4) | (fat[offset + 1] << 4)) & 0x0fff);
  }
  return static_cast<uint16_t>((fat[offset] | ((fat[offset + 1] & 0x0f) << 8)) & 0x0fff);
}

static void testFormat()
{
  EspUsbDeviceMscFatRamDisk disk(storage, sizeof(storage));
  check(disk.format("ESPUSB"), "format");
  check(disk.blockCount() == sizeof(storage) / 512, "block_count");
  check(disk.blockSize() == 512, "block_size");
  check(disk.byteSize() == sizeof(storage), "byte_size");

  check(storage[0] == 0xeb && storage[1] == 0x3c && storage[2] == 0x90, "boot_jump");
  check(memcmp(storage + 3, "MSDOS5.0", 8) == 0, "boot_oem");
  check(le16(storage + 11) == 512, "boot_sector_size");
  check(storage[13] == 1, "boot_sectors_per_cluster");
  check(le16(storage + 14) == 1, "boot_reserved");
  check(storage[16] == 2, "boot_fat_count");
  check(le16(storage + 17) == 64, "boot_root_entries");
  check(le16(storage + 19) == sizeof(storage) / 512, "boot_total_sectors");
  check(storage[21] == 0xf8, "boot_media");
  check(memcmp(storage + 43, "ESPUSB     ", 11) == 0, "boot_label");
  check(memcmp(storage + 54, "FAT12   ", 8) == 0, "boot_type");
  check(storage[510] == 0x55 && storage[511] == 0xaa, "boot_signature");
}

static void testFiles()
{
  EspUsbDeviceMscFatRamDisk disk(storage, sizeof(storage));
  check(disk.format("ESPUSB"), "files_format");
  check(disk.addTextFile("README.TXT", "hello\r\n"), "add_readme");
  check(!disk.addTextFile("README.TXT", "duplicate\r\n"), "reject_duplicate");
  check(!disk.addTextFile("TOO-LONG-NAME.TXT", "bad\r\n"), "reject_add_long_name");
  check(!disk.addTextFile("BAD+NAME.TXT", "bad\r\n"), "reject_bad_char");
  check(disk.addFile("EMPTY.BIN", reinterpret_cast<const uint8_t *>(""), 0), "add_empty");

  static uint8_t payload[700];
  for (size_t i = 0; i < sizeof(payload); ++i)
  {
    payload[i] = static_cast<uint8_t>(i & 0xff);
  }
  check(disk.addFile("DATA.BIN", payload, sizeof(payload)), "add_data");

  check(disk.exists("README.TXT"), "exists_readme");
  check(disk.exists("data.bin"), "exists_casefold");
  check(!disk.exists("LONGFILENAME.TXT"), "reject_long_name");
  check(disk.fileSize("README.TXT") == 7, "readme_size");
  check(disk.fileSize("EMPTY.BIN") == 0, "empty_size");
  check(disk.fileSize("DATA.BIN") == sizeof(payload), "data_size");

  uint8_t readme[16] = {};
  check(disk.readFile("README.TXT", readme, sizeof(readme)) == 7, "read_readme_size");
  check(memcmp(readme, "hello\r\n", 7) == 0, "read_readme_data");

  uint8_t data[700] = {};
  check(disk.readFile("DATA.BIN", data, sizeof(data)) == sizeof(data), "read_data_size");
  check(memcmp(data, payload, sizeof(data)) == 0, "read_data_content");

  uint8_t partial[10] = {};
  check(disk.readFile("DATA.BIN", partial, sizeof(partial)) == sizeof(partial), "read_partial_size");
  check(memcmp(partial, payload, sizeof(partial)) == 0, "read_partial_content");
  check(disk.readFile("MISSING.BIN", partial, sizeof(partial)) == 0, "read_missing");

  const uint16_t sectorsPerFat = le16(storage + 22);
  const uint8_t *root = storage + static_cast<size_t>(1 + 2 * sectorsPerFat) * 512;
  check(memcmp(root, "README  TXT", 11) == 0, "root_readme_name");
  check(root[11] == 0x20, "root_readme_attr");
  check(le16(root + 26) == 2, "root_readme_cluster");
  check(le32(root + 28) == 7, "root_readme_size");

  const uint8_t *second = root + 32;
  check(memcmp(second, "EMPTY   BIN", 11) == 0, "root_empty_name");
  check(le16(second + 26) == 3, "root_empty_cluster");
  check(le32(second + 28) == 0, "root_empty_size");

  const uint8_t *third = root + 64;
  check(memcmp(third, "DATA    BIN", 11) == 0, "root_data_name");
  check(le16(third + 26) == 4, "root_data_cluster");
  check(le32(third + 28) == sizeof(payload), "root_data_size");

  const uint8_t *fat = storage + 512;
  check(fat12Entry(fat, 2) == 0x0fff, "fat_readme_eoc");
  check(fat12Entry(fat, 3) == 0x0fff, "fat_empty_eoc");
  check(fat12Entry(fat, 4) == 5 && fat12Entry(fat, 5) == 0x0fff, "fat_data_chain");
}

static void testInvalidDisks()
{
  uint8_t tiny[8 * 512] = {};
  EspUsbDeviceMscFatRamDisk tooSmall(tiny, sizeof(tiny));
  check(!tooSmall.format("SMALL"), "reject_too_small");

  EspUsbDeviceMscFatRamDisk nullDisk(nullptr, sizeof(storage));
  check(!nullDisk.format("NULL"), "reject_null_storage");
}

static void testAttachAndEject()
{
  EspUsbDevice device;
  EspUsbDeviceMsc msc(device);
  EspUsbDeviceMscFatRamDisk disk(storage, sizeof(storage));
  bool ejected = false;

  check(disk.format("ESPUSB"), "attach_format");
  disk.onEject([&ejected]()
               { ejected = true; });
  check(disk.attach(msc), "fat_attach");
  msc.mediaPresent(true);
  msc.isWritable(true);

  check(msc.testUnitReady(), "msc_ready");
  uint32_t blocks = 0;
  uint16_t blockSize = 0;
  msc.capacity(&blocks, &blockSize);
  check(blocks == sizeof(storage) / 512 && blockSize == 512, "msc_capacity");

  uint8_t buffer[512] = {};
  check(msc.read10(0, 0, buffer, sizeof(buffer)) == sizeof(buffer), "msc_read_boot");
  check(buffer[510] == 0x55 && buffer[511] == 0xaa, "msc_read_boot_sig");

  buffer[0] = 0x42;
  check(msc.write10(10, 0, buffer, sizeof(buffer)) == sizeof(buffer), "msc_write_block");
  check(storage[10 * 512] == 0x42, "msc_write_stored");

  check(msc.startStop(0, false, true), "msc_start_stop");
  check(ejected, "eject_callback");
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  Serial.println("TEST_BEGIN fat_ramdisk");
  testFormat();
  testFiles();
  testInvalidDisks();
  testAttachAndEject();
  Serial.printf("TEST_END pass=%d fail=%d\n", passCount, failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
  Serial.flush();
}

void loop()
{
}
