// en: Expose an SPI SD card to the USB host as block storage.
// ja: SPIのSDカードをUSBホストへblockストレージとして公開する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include <SD.h>
#include "EspUsbDevice.h"

#if !ESP_USB_DEVICE_HAS_ARDUINO_SD
#error "Arduino SD library is required for this example"
#endif

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceMsc msc(device);
EspUsbDeviceMscSdCard sdMsc(SD);

static constexpr uint8_t SD_CS_PIN = SS;

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  if (!sdMsc.begin(SD_CS_PIN, SPI, 4000000, "/sd", 1))
  {
    Serial.println("SD_BEGIN_FAILED");
    return;
  }

  sdMsc.onEject([]()
                { Serial.println("SD_EJECT"); });

  msc.vendorID("ESP32");
  msc.productID("SD MSC");
  msc.productRevision("1.0");
  msc.mediaPresent(true);
  msc.isWritable(true);

  // en: Attach the medium before starting USB; host and sketch must not edit its filesystem independently.
  // ja: USB開始前に媒体を接続する。ホストとスケッチが別々にfilesystemを書き換えないようにする。
  if (!sdMsc.attach(msc))
  {
    Serial.println("MSC_ATTACH_FAILED");
    return;
  }

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4009;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice SD MSC";
  config.serialNumber = "espusb-sd-msc";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.printf("USB SD MSC ready blocks=%lu block_size=%u\n",
                static_cast<unsigned long>(sdMsc.blockCount()),
                sdMsc.blockSize());
}

void loop()
{
  delay(1000);
}
