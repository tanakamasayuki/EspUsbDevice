// Expose an SPI-connected SD card to the USB host as block storage.
// Wiring, ownership rules and host-side steps: README.md / README.ja.md.
#include <SD.h>
#include <SPI.h>
#include <EspUsbDevice.h>

#if !ESP_USB_DEVICE_HAS_ARDUINO_SD
#error "Arduino SD library is required for this example"
#endif

EspUsbDevice device;
EspUsbDeviceMsc msc(device);
EspUsbDeviceMscSdCard sdMsc(SD);

// The board's default SPI pins. Change all four to match your socket.
constexpr uint8_t SD_SCK_PIN = SCK;
constexpr uint8_t SD_MISO_PIN = MISO;
constexpr uint8_t SD_MOSI_PIN = MOSI;
constexpr uint8_t SD_CS_PIN = SS;
constexpr uint32_t SD_SPI_HZ = 4000000;

// Set on the USB worker by the eject callback, acted on in loop().
volatile bool ejected = false;

void setup() {
  Serial.begin(115200); // Separate debug connection.
  delay(1500);
  Serial.printf("SD pins SCK=%u MISO=%u MOSI=%u CS=%u\n",
                SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

  // SD.begin() inside sdMsc.begin() reuses an SPI bus that is already started.
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  // Mounts the card (as "/sd", 1 open file) and reads its sector count. The
  // sketch then leaves the filesystem alone while the host owns it.
  if (!sdMsc.begin(SD_CS_PIN, SPI, SD_SPI_HZ, "/sd", 1)) {
    Serial.println("SD_BEGIN_FAILED - check wiring, power, CS pin and card");
    return;
  }
  sdMsc.onEject([]() { ejected = true; });

  EspUsbMscConfig mscConfig;
  mscConfig.name = "SD card";
  // SD is read one 512-byte sector at a time either way; a larger staging
  // buffer only cuts the number of USB round trips. 4096 is a multiple of 512,
  // so it is valid at FS and at HS (ESP32-P4).
  mscConfig.transferBytes = 4096;
  esp_err_t error = msc.configure(mscConfig);
  if (error != ESP_OK) { Serial.printf("MSC_CONFIGURE_FAILED %s\n", esp_err_to_name(error)); return; }
  msc.vendorID("ESP32");
  msc.productID("SD MSC");
  msc.productRevision("1.0");
  // For a read-only card, call sdMsc.readOnly(true) before attach(): attach()
  // then reports the card write-protected (MODE SENSE WP), so the host mounts
  // it read-only.
  msc.isWritable(true);

  // Installs read/write and START STOP UNIT, and the card's geometry.
  if (!sdMsc.attach(msc)) { Serial.println("MSC_ATTACH_FAILED"); return; }
  msc.mediaPresent(true);

  EspUsbDeviceConfig identity;
  identity.vid = 0x303a;
  identity.pid = 0x4009;
  identity.manufacturer = "EspUsb";
  identity.product = "EspUsbDevice SD MSC";
  identity.serialNumber = "espusb-sd-msc";
  if (!device.begin(identity)) {
    Serial.printf("USB_BEGIN_FAILED %s msc=%s\n", device.lastErrorName(),
                  esp_err_to_name(msc.startError()));
    return;
  }
  Serial.printf("USB SD MSC ready blocks=%lu block_size=%u\n",
                (unsigned long)sdMsc.blockCount(), sdMsc.blockSize());
}

void loop() {
  if (ejected) {
    ejected = false;
    // The host has released the card. Report the medium as removed so a host
    // that keeps polling does not pick it up again behind our back.
    msc.mediaPresent(false);
    Serial.println("SD_EJECT - send 'r' on Serial to give the card back to the host");
  }
  while (Serial.available()) {
    if (Serial.read() == 'r') {
      // The host sees a medium change (UNIT ATTENTION) and mounts it again.
      msc.mediaPresent(true);
      Serial.println("SD_REINSERTED");
    }
  }
  delay(10);
}
