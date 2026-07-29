#include "EspUsbDevice.h"

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "p4_hs_bulk requires ESP32-P4"
#endif

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);

static uint64_t echoedBytes = 0;
static uint32_t transferErrors = 0;

void setup()
{
  Serial.begin(115200);
  delay(1500);

  vendor.onRx([](size_t available)
              {
                uint8_t buffer[512];
                while (available > 0)
                {
                  const size_t received =
                      vendor.read(buffer, min(available, sizeof(buffer)));
                  if (received == 0)
                  {
                    transferErrors++;
                    break;
                  }

                  // The PC sends one packet and waits for its echo before sending
                  // the next one, so the 512-byte TinyUSB TX FIFO has room here.
                  const size_t written = vendor.write(buffer, received);
                  if (written != received)
                  {
                    transferErrors++;
                    break;
                  }
                  vendor.flush();
                  echoedBytes += written;
                  available = vendor.available();
                }
              });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4041;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice P4 HS Bulk Test";
  config.serialNumber = "espusb-p4-hs-bulk";

  // Use the ESP32-P4 High-Speed Device controller (TinyUSB rhport 1) and the
  // board connector wired to its external UTMI HS PHY. This is not the USB
  // Serial/JTAG connector and not the GPIO26/GPIO27 Full-Speed pair.
  // Connector naming and VBUS/CC wiring are board-specific; check the schematic.
  config.controller = EspUsbController::HighSpeed;

  if (!device.begin(config))
  {
    Serial.printf("P4_HS_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("P4_HS_BULK_READY");
}

void loop()
{
  static uint32_t lastStatusMs = 0;
  const uint32_t now = millis();
  if (now - lastStatusMs >= 5000)
  {
    lastStatusMs = now;
    Serial.printf("P4_HS_BULK_STATUS mounted=%u bytes=%llu errors=%lu\n",
                  vendor.mounted() ? 1 : 0,
                  static_cast<unsigned long long>(echoedBytes),
                  static_cast<unsigned long>(transferErrors));
  }
  delay(1);
}
