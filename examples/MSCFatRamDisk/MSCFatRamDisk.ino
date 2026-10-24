// en: Read back a host-written file after ejecting a FAT RAM disk.
// ja: FAT RAM diskを取り出した後、ホストが書いたファイルを読む。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceMsc msc(device);

// en: Keep the example small enough for the ESP32-S2's internal DRAM after the library-owned TinyUSB class buffers are linked. Larger RAM disks should use PSRAM explicitly; persistent or substantially larger media should use SD.
// ja: USB機能のbufferとRAM diskを合わせた使用量を考え、必要ならPSRAMや実媒体を選ぶ。
static uint8_t storage[64 * 1024];
EspUsbDeviceMscFatRamDisk disk(storage, sizeof(storage));

static bool scanRequested = false;

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  // en: Prepare the volume before USB exposes it; this operation is not a read-only inspection.
  // ja: USBへ公開する前にvolumeを準備する。読み取りだけの確認操作ではない。
  if (!disk.format("ESPUSB"))
  {
    Serial.println("FAT_FORMAT_FAILED");
    return;
  }

  disk.addTextFile("README.TXT",
                   "EspUsbDevice FAT RAM disk\r\n"
                   "\r\n"
                   "Copy CONFIG.TXT here, eject the drive, and watch Serial.\r\n");

  disk.onEject([]()
               {
                 scanRequested = true;
                 Serial.println("MSC_EJECT");
               });

  msc.vendorID("ESP32");
  msc.productID("FAT RAM DISK");
  msc.productRevision("1.0");
  msc.mediaPresent(true);
  msc.isWritable(true);

  // en: Attach the medium before starting USB; host and sketch must not edit its filesystem independently.
  // ja: USB開始前に媒体を接続する。ホストとスケッチが別々にfilesystemを書き換えないようにする。
  if (!disk.attach(msc))
  {
    Serial.println("MSC_ATTACH_FAILED");
    return;
  }

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4008;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice FAT RAM Disk";
  config.serialNumber = "espusb-fat-ramdisk";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.printf("USB FAT RAM disk ready blocks=%lu bytes=%u\n",
                static_cast<unsigned long>(disk.blockCount()),
                static_cast<unsigned>(disk.byteSize()));
}

void loop()
{
  if (!scanRequested)
  {
    delay(10);
    return;
  }
  scanRequested = false;

  if (!disk.exists("CONFIG.TXT"))
  {
    Serial.println("CONFIG_NOT_FOUND");
    return;
  }

  const size_t size = disk.fileSize("CONFIG.TXT");
  static uint8_t buffer[256];
  const size_t readSize = disk.readFile("CONFIG.TXT", buffer, min(size, sizeof(buffer) - 1));
  buffer[readSize] = '\0';

  Serial.printf("CONFIG_SIZE %u\n", static_cast<unsigned>(size));
  Serial.printf("CONFIG_BEGIN\n%s\nCONFIG_END\n", reinterpret_cast<char *>(buffer));
}
