#include <SD.h>
#include "EspUsbDevice.h"

#if !ESP_USB_DEVICE_HAS_ARDUINO_SD
#error "Arduino SD library is required for this example"
#endif

EspUsbDevice device;
EspUsbDeviceMsc msc(device);
EspUsbDeviceMscSdCard sdMsc(SD);

static constexpr uint8_t SD_CS_PIN = SS;

void setup()
{
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

  if (!sdMsc.attach(msc))
  {
    Serial.println("MSC_ATTACH_FAILED");
    return;
  }

  EspUsbDeviceConfig config;
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4009;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice SD MSC";
  config.serialNumber = "espusb-sd-msc";

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
