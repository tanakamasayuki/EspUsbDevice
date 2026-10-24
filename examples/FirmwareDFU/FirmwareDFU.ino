// en: Update the application through the DFU download function.
// ja: DFU download機能からapplicationを更新する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"


// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware");

static volatile uint32_t reportedKiB = 0;

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  if (!EspUsbDeviceFirmwareUpdate::available())
  {
    // en: Nothing below can work under a single-app partition scheme. Say it once, clearly, instead of letting a 300 KB upload fail on its last block.
    // ja: 更新先application領域が必要。partition条件が合わなければ更新へ進まない。
    Serial.println("NO_OTA_PARTITION - select a partition scheme with two app partitions");
  }
  else
  {
    Serial.printf("Update target: %s (%u bytes)\n",
                  EspUsbDeviceFirmwareUpdate::targetLabel(),
                  static_cast<unsigned>(EspUsbDeviceFirmwareUpdate::capacity()));
  }

  // en: Progress runs on the usbd task, once per DFU block. Printing every block would be the slowest thing in the transfer, so this reports each kilobyte. There is no percentage to show: DFU 1.1 never tells the device how long the image is.
  // ja: 進捗通知はUSBタスク。表示の頻度を抑え、USB処理を待たせない。
  dfu.onProgress([](size_t written)
                 {
                   const uint32_t kiB = written / 1024;
                   if (kiB != reportedKiB)
                   {
                     reportedKiB = kiB;
                     Serial.printf("DFU %u KiB\n", static_cast<unsigned>(kiB));
                   }
                 });

  // en: The image is written and verified by the time this runs, and the boot partition has already moved. Returning false would put it back.
  // ja: この時点ではimageの書き込み・検証後。完了時の再起動を受け付けるか判断する。
  dfu.onComplete([]() -> bool
                 {
                   Serial.println("DFU complete - restarting into the new firmware");
                   return true;
                 });

  dfu.onError([](uint8_t status)
              {
                // en: DFU 1.1 status codes: 3 = errWRITE (the image was refused before anything was written, usually a file that is not an ESP application), 7 = errVERIFY (it did not survive verification). dfu-util prints the same number.
                // ja: DFUのstatusを読み、書き込み拒否と消去等の異常を区別する。
                Serial.printf("DFU failed, status %u\n", static_cast<unsigned>(status));
              });

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin())
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Ready. dfu-util -D <firmware.bin>");
}

void loop()
{
  static bool confirmed = false;
  if (!confirmed && device.ready())
  {
    confirmed = true;
    EspUsbDeviceFirmwareUpdate::markValid();
  }
  delay(20);
}
