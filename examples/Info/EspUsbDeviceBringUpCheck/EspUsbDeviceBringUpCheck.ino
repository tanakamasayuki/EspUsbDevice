// First sketch for a new board: separates "the device started" from "the Host
// configured it", and prints a checklist when enumeration never happens.
// A HID keyboard is registered but no key is ever sent.
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

bool beginOk = false, lastReady = false, lastSuspended = false;
bool checklistPrinted = false;
uint32_t beginMs = 0, lastStatusMs = 0;

const char *targetName() {
#if defined(CONFIG_IDF_TARGET_ESP32S2)
  return "ESP32-S2";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  return "ESP32-S3";
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
  return "ESP32-P4";
#else
  return "unsupported-target";
#endif
}

const char *controllerName(EspUsbController controller) {
  switch (controller) {
  case EspUsbController::FullSpeed: return "FullSpeed";
  case EspUsbController::HighSpeed: return "HighSpeed";
  default: return "Auto";
  }
}

// Auto picks the high-speed port when the SoC has one (P4), else full speed.
const char *resolvedControllerName(EspUsbController controller) {
  if (controller != EspUsbController::Auto) return controllerName(controller);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  return "HighSpeed (Auto on P4)";
#else
  return "FullSpeed (Auto)";
#endif
}

void printChecklist() {
  Serial.println("CHECKLIST the host has not enumerated this board yet:");
  Serial.println("  1. Cable      - is it a data cable? A charge-only cable has no D+/D-.");
  Serial.println("  2. Connector  - is the cable in the OTG/device connector, not the");
  Serial.println("                  USB-UART or USB Serial/JTAG one? Check the schematic;");
  Serial.println("                  the silkscreen is not reliable.");
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
  Serial.println("  3. Build      - is 'USB Mode' set to 'USB-OTG (TinyUSB)' (USBMode=default)?");
  Serial.println("                  The ESP32-S3 IDE default is 'Hardware CDC and JTAG'.");
#else
  Serial.println("  3. Build      - the core's own USB must not claim the OTG controller.");
#endif
  Serial.println("                  Do not enable the core's USB CDC/MSC/DFU on boot or call");
  Serial.println("                  USB.begin(); this library owns the OTG controller.");
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  Serial.println("  4. P4 port    - HighSpeed uses the HS port and its UTMI PHY;");
  Serial.println("                  FullSpeed uses the FS port (GPIO26/27 by default).");
  Serial.println("                  Set config.controller to match the connector you used.");
#else
  Serial.println("  4. Power      - is the board powered? A bus-powered board that lost");
  Serial.println("                  VBUS when you moved the cable is not running at all.");
#endif
  Serial.println("  5. Host side  - Linux: 'dmesg -w' while plugging. Windows: Device");
  Serial.println("                  Manager. If the host logs nothing, it is electrical.");
  Serial.println("  6. Descriptor - if the host logs an error instead of nothing, run");
  Serial.println("                  EspUsbDeviceDescriptorDump and compare.");
}

void setup() {
  Serial.begin(115200);  // Debug port, separate from the USB device connector.
  delay(1500);
  Serial.println();
  Serial.println("=== EspUsbDevice bring-up check ===");
  Serial.printf("TARGET %s\n", targetName());
  Serial.printf("CORE arduino-esp32 %d.%d.%d\n", ESP_ARDUINO_VERSION_MAJOR,
      ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
  Serial.printf("LIBRARY EspUsbDevice %s\n", ESPUSBDEVICE_VERSION_STR);

  // An LED output report proves the host -> device direction works.
  // Runs on the USB worker; keep it short. Register before begin().
  if (keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report) {
        Serial.printf("HOST_OUTPUT_REPORT leds=0x%02x num=%u caps=%u scroll=%u\n",
            report.leds, report.numLock, report.capsLock, report.scrollLock);
      }) != ESP_OK) {
    Serial.println("onOutputReport rejected");
  }

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4050;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice BringUpCheck";
  config.serialNumber = "espusb-bringup";
  // On ESP32-P4, match the connector the cable goes to:
  // config.controller = EspUsbController::FullSpeed;
  Serial.printf("CONTROLLER requested=%s resolved=%s\n",
      controllerName(config.controller), resolvedControllerName(config.controller));

  beginOk = device.begin(config);
  beginMs = millis();
  if (!beginOk) {
    Serial.printf("BEGIN failed error=%s\n", device.lastErrorName());
    Serial.println("  A failure here is local to the board - the host is not involved yet.");
    Serial.println("  ESP_ERR_INVALID_SIZE  : the descriptor or endpoint budget does not fit");
    Serial.println("                          this controller. Remove a Function or use the P4 HS port.");
    Serial.println("  ESP_ERR_NOT_SUPPORTED : no USB device controller for this target/controller.");
    Serial.println("  ESP_ERR_NO_MEM        : out of heap for descriptors or Function buffers.");
    Serial.println("  ESP_ERR_INVALID_ARG   : a config value or Function setting is invalid.");
    Serial.println("  ESP_ERR_INVALID_STATE : already started, or a Function was added after begin().");
    return;
  }
  const uint8_t *configuration = device.configurationDescriptor(0);
  Serial.println("BEGIN ok");
  Serial.printf("DESCRIPTOR config_bytes=%u\n",
      configuration ? unsigned(configuration[2] | (configuration[3] << 8)) : 0u);
  Serial.printf("VID_PID %04x:%04x\n", config.vid, config.pid);
  Serial.println("Now connect the device connector to a host and watch for MOUNTED.");
}

void loop() {
  if (!beginOk) {
    delay(1000);
    return;
  }
  const uint32_t now = millis();
  const bool ready = device.ready();
  if (ready != lastReady) {
    lastReady = ready;
    if (ready) {
      const EspUsbBusSpeed speed = device.speed();
      Serial.printf("MOUNTED t=%lums speed=%s hid_packet=%u\n", (unsigned long)(now - beginMs),
                    speed == EspUsbBusSpeed::High ? "high" : speed == EspUsbBusSpeed::Full ? "full" : "none",
                    unsigned(keyboard.inputPacketSize()));
      Serial.println("  The host completed enumeration and selected a configuration.");
      Serial.println("  Press CapsLock on the host keyboard to check host -> device.");
      checklistPrinted = true;  // Enumerated at least once; stop nagging.
    } else {
      Serial.printf("UNMOUNTED t=%lums\n", (unsigned long)(now - beginMs));
      Serial.println("  Cable removed, host reset the bus, or the host deselected");
      Serial.println("  the configuration.");
    }
  }
  // Suspend keeps the configuration: ready() stays true while suspended.
  const bool suspended = device.suspended();
  if (suspended != lastSuspended) {
    lastSuspended = suspended;
    Serial.printf("%s t=%lums\n", suspended ? "SUSPENDED" : "RESUMED",
        (unsigned long)(now - beginMs));
  }

  if (!ready && !checklistPrinted && now - beginMs > 10000) {
    checklistPrinted = true;
    printChecklist();
  }
  if (now - lastStatusMs >= 5000) {
    lastStatusMs = now;
    Serial.printf("STATUS mounted=%u suspended=%u t=%lums leds=0x%02x\n", ready,
        suspended, (unsigned long)(now - beginMs), keyboard.ledState().leds);
  }
  delay(10);
}
