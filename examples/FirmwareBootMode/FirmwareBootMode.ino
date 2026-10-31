// Restart into the chip's ROM download loader ("boot mode") from a running
// sketch, so esptool can rewrite the whole flash without touching the board.
// Three requests lead there: a 1200-baud touch on the CDC port, DFU_DETACH
// (`dfu-util -e`), or 'b' sent on the CDC port. Details: README.md / README.ja.md.
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial port(device, "Console");
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime, "Bootloader");

// Set from USB callbacks (on the USB worker), acted on in loop(): restarting
// inside a callback would cut off the transfer the host is still finishing.
volatile bool bootloaderRequested = false;
volatile bool romDfuRequested = false;

void setup() {
  Serial.begin(115200); // Separate debug connection.
  delay(1500);

  EspUsbBulkStreamConfig serialConfig;
  serialConfig.rxCapacity = 64;
  serialConfig.txCapacity = 256;
  serialConfig.transferBytes = 64;
  esp_err_t error = port.configure(serialConfig);
  if (error != ESP_OK) { Serial.printf("CDC_CONFIGURE_FAILED %s\n", esp_err_to_name(error)); return; }

  // The 1200-baud touch. A host that merely opens the port at another speed
  // is not asking for anything, so nothing happens then.
  port.onLineCoding([](const EspUsbDeviceCdcLineCoding &coding) {
    if (coding.baud == 1200) bootloaderRequested = true;
  });

  // DFU_DETACH. With no handler the Runtime function restarts into the ROM
  // loader by itself ~100 ms after the request completes. Taking the handler
  // moves that restart into loop(), which is where a sketch would log, save
  // state, or shut peripherals down first. dfu.onDetach() could also refuse
  // the request (return false = STALL) before it is accepted.
  dfu.setDetachHandler([]() { bootloaderRequested = true; });

  if (!device.begin()) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Ready. Touch the CDC port at 1200 baud, run dfu-util -e, or send 'b'"
                 " ('d' asks for the S2/S3 ROM DFU instead).");
}

void loop() {
  // Commands on the CDC port, read here rather than in a USB callback.
  while (port.available() > 0) {
    const int c = port.read();
    if (c == 'b') bootloaderRequested = true;
    if (c == 'd') romDfuRequested = true;
  }

  if (romDfuRequested) {
    romDfuRequested = false;
    Serial.println("Restarting into the ROM DFU device");
    Serial.flush();
    // Returns only when refused: ESP32-P4, or an ESP32-S3 whose USB_PHY_SEL
    // eFuse is not burned (the connector would go dark).
    device.rebootToRomDfu();
    Serial.printf("ROM_DFU_UNSUPPORTED %s\n", device.lastErrorName());
    port.printf("ROM DFU refused: %s\n", device.lastErrorName());
  }

  if (bootloaderRequested) {
    bootloaderRequested = false;
    Serial.println("Restarting into the ROM download loader");
    Serial.flush();
    // Detaches from USB, sets the target's download-boot flag and restarts.
    // Does not return on the chips this library supports.
    device.rebootToBootloader();
    Serial.printf("REBOOT_UNSUPPORTED %s\n", device.lastErrorName());
  }
  delay(20);
}
