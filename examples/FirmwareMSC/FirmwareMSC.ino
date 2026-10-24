// en: Accept an application image copied to a virtual firmware volume.
// ja: 仮想firmware volumeへコピーしたapplication imageを受け付ける。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"


// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceMsc msc(device);

// en: Metadata plus scratch. The scratch area is what absorbs the files a host leaves behind - `System Volume Information` on Windows, `.fseventsd` and `.Spotlight-V100` on macOS - so do not make it tight.
// ja: RAMはvolumeのmetadataと一時領域に使い、image全体を保持する領域ではない。
static uint8_t diskStorage[16 * 1024];
EspUsbDeviceMscFirmwareDisk disk(diskStorage, sizeof(diskStorage));

static uint32_t reportedKiB = 0;

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  // en: Prepare the volume before USB exposes it; this operation is not a read-only inspection.
  // ja: USBへ公開する前にvolumeを準備する。読み取りだけの確認操作ではない。
  if (!disk.begin("ESPUSB"))
  {
    // en: Either there is no second application partition, or diskStorage is too small for the volume this partition would need.
    // ja: 更新先partitionや作業領域の条件を確認し、使えなければ開始を止める。
    Serial.println("DISK_BEGIN_FAILED - check the partition scheme has two app partitions");
    return;
  }
  Serial.printf("Update target: %s (%u bytes), volume %u sectors, %u of them in RAM\n",
                EspUsbDeviceFirmwareUpdate::targetLabel(),
                static_cast<unsigned>(EspUsbDeviceFirmwareUpdate::capacity()),
                static_cast<unsigned>(disk.blockCount()),
                static_cast<unsigned>(disk.ramSectorCount()));

  disk.addTextFile("README.TXT",
                   "Copy a firmware .bin onto this drive.\r\n"
                   "The board writes it to the spare OTA partition, checks it,\r\n"
                   "and restarts into it. Eject the drive if nothing happens.\r\n");

  disk.onProgress([](size_t written)
                  {
                    const uint32_t kiB = written / 1024;
                    if (kiB != reportedKiB)
                    {
                      reportedKiB = kiB;
                      Serial.printf("%u KiB\n", static_cast<unsigned>(kiB));
                    }
                  });

  disk.onComplete([]() -> bool
                  {
                    Serial.println("Verified - restarting into the new firmware");
                    return true;
                  });

  disk.onError([](esp_err_t error)
               {
                 // en: ESP_ERR_INVALID_STATE is the host writing out of order. Anything else came from the flash or the verification.
                 // ja: 順序の合わない書き込みは状態異常として拒否する。受理された長さと進捗を確認する。
                 Serial.printf("Update failed: %s\n", esp_err_to_name(error));
                 reportedKiB = 0;
               });

  // en: Attach the medium before starting USB; host and sketch must not edit its filesystem independently.
  // ja: USB開始前に媒体を接続する。ホストとスケッチが別々にfilesystemを書き換えないようにする。
  if (!disk.attach(msc))
  {
    Serial.println("DISK_ATTACH_FAILED");
    return;
  }
  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin())
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Ready - a drive should appear on the host");
}

void loop()
{
  // en: Confirm this image once the host has it mounted, cancelling any pending bootloader rollback. A no-op unless the bootloader was built with CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE.
  // ja: このアプリが動作したことを確認して検証状態を更新する。署名・認証の代わりではない。
  static bool confirmed = false;
  if (!confirmed && device.ready())
  {
    confirmed = true;
    EspUsbDeviceFirmwareUpdate::markValid();
  }
  delay(20);
}
