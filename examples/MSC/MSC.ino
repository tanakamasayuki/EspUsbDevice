// en: Expose raw RAM blocks as USB mass storage.
// ja: RAM上の生blockをUSBストレージとして公開する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
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
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
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

  // en: Attach the medium before starting USB; host and sketch must not edit its filesystem independently.
  // ja: USB開始前に媒体を接続する。ホストとスケッチが別々にfilesystemを書き換えないようにする。
  if (!disk.attach(msc))
  {
    Serial.println("MSC_ATTACH_FAILED");
    return;
  }

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4007;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice MSC";
  config.serialNumber = "espusb-msc";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
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
